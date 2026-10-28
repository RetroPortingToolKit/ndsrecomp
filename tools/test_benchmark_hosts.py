import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import eligible_measurement_host as eligibility

from benchmark_host_agent import assess, FileLock, active_claims, FORMAT
from benchmark_hosts import Pool, transport, NoCapacity


class CapacityTests(unittest.TestCase):
    def setUp(self):
        self.sample=dict(physical_cores=4,cpu_free_cores=3,memory_available_gib=8,
            memory_total_gib=16,gpu_busy_percent=30,storage_free_gib=100)
        self.request=dict(cpu=1.5,memory=2,gpu=20,disk=1)

    def test_busy_services_are_allowed_with_capacity(self):
        self.assertTrue(assess(self.sample,self.request)['eligible'])

    def test_saturated_and_unknown_resources_reject(self):
        for field,value in (('cpu_free_cores',1),('gpu_busy_percent',95),
                            ('gpu_busy_percent',None),('memory_available_gib',1),
                            ('storage_free_gib',1)):
            with self.subTest(field=field,value=value):
                self.assertFalse(assess({**self.sample,field:value},self.request)['eligible'])

    def test_pending_budget_is_subtracted(self):
        claim=dict(requested=self.request,phase='pending')
        self.assertFalse(assess(self.sample,self.request,[claim])['eligible'])

    def test_running_budget_not_double_charged(self):
        claim=dict(requested=self.request,phase='running')
        self.assertTrue(assess(self.sample,self.request,[claim])['eligible'])

    def test_total_claims_still_bound_peak_capacity(self):
        claim=dict(requested={**self.request,'cpu':3},phase='running')
        self.assertFalse(assess(self.sample,self.request,[claim])['eligible'])

    def test_cpu_only_can_use_a_host_without_gpu_telemetry(self):
        self.assertTrue(assess({**self.sample,'gpu_busy_percent':None},
            {**self.request,'gpu':0})['eligible'])

    def test_negative_budget_never_confers_capacity(self):
        self.assertFalse(assess(self.sample,{**self.request,'cpu':-1})['eligible'])
        for value in (float('nan'),float('inf')):
            self.assertFalse(assess(self.sample,{**self.request,'cpu':value})['eligible'])

    def test_live_os_lock_and_dead_lease_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            record=root/('lease-'+'a'*32+'.json')
            data=dict(format=FORMAT,requested=self.request,phase='pending')
            lock=FileLock(record.with_suffix('.lock')).acquire()
            try:
                record.write_text(json.dumps(data))
                self.assertEqual(active_claims(root),[data])
            finally:
                lock.close()
            self.assertEqual(active_claims(root),[])
            self.assertFalse(record.exists())

    def test_nds_adapter_accepts_other_game_and_build_with_capacity(self):
        sample={**self.sample,'logical_cpus':8,'cpu_busy_percent':25}
        previous=dict(time=1,processes=[dict(pid=1,name='cmake.exe',cpu=1),dict(pid=2,name='OtherRecomp.exe',cpu=1)])
        current=dict(time=3,processes=[dict(pid=1,name='cmake.exe',cpu=2),dict(pid=2,name='OtherRecomp.exe',cpu=2)])
        with patch.object(eligibility.capacity,'probe',return_value=sample):
            receipt=eligibility.assess(previous,current,set(),set(),[dict(pid=2,engine='pid_2_luid_0_eng_0',utilization=30)])
        self.assertTrue(receipt['qualified'])

    def test_nds_adapter_rejects_capacity_instead_of_process_name(self):
        sample={**self.sample,'logical_cpus':8,'cpu_busy_percent':99}
        previous=dict(time=1,processes=[])
        current=dict(time=3,processes=[])
        with patch.object(eligibility.capacity,'probe',return_value=sample):
            receipt=eligibility.assess(previous,current,set(),set(),[])
        self.assertFalse(receipt['qualified'])

    def test_exhausted_pool_retains_all_rejection_receipts(self):
        pool=Pool()
        with patch.object(pool,'candidates',return_value=pool.hosts), patch.object(pool,'connect',return_value=dict(eligible=False)):
            with self.assertRaises(NoCapacity) as error:
                pool.reserve(self.request,'unit')
        self.assertEqual(len(error.exception.receipts),3)

    def test_deck_aliases_share_one_ledger(self):
        deck=next(h for h in Pool().hosts if h['id']=='steamdeck')
        self.assertEqual(len(deck['aliases']),2)
        self.assertEqual(len([h for h in Pool().hosts if h['os']=='linux']),1)

    def test_remote_windows_command_has_bounded_quoted_bootstrap(self):
        server=next(h for h in Pool().hosts if h['id']=='server')
        argv=transport(server,[server['python'],'-c',"print('literal $ and backtick `')"],'server')
        self.assertLess(len(' '.join(argv)),8000)
        self.assertIn('BatchMode=yes',argv)


if __name__=='__main__':
    unittest.main()
