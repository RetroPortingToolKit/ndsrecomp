#include "native_model.h"
#include <cstdio>
#include <stdexcept>
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
    nds_native_model::TextureTransform t;t.width=64;t.height=32;
    auto m=nds_native_model::TextureMatrix(t);
    Check(m[0]==4096&&m[5]==4096&&m[12]==0&&m[13]==0&&m[15]==4096,"identity texture transform");
    t.translate_s=2048;t.translate_t=1024;m=nds_native_model::TextureMatrix(t);
    Check(m[12]==-2097152&&m[13]==524288,"resource translation uses texture dimensions and signed offsets");
    t.translate_s=t.translate_t=0;t.scale_s=8192;t.scale_t=12288;m=nds_native_model::TextureMatrix(t);
    Check(m[0]==8192&&m[5]==12288&&m[13]==-4194304,"texture scale retains lower-edge origin");
    t={};t.width=8;t.height=16;t.sine=4096;t.cosine=0;m=nds_native_model::TextureMatrix(t);
    Check(m[0]==0&&m[1]==-8192&&m[4]==2048&&m[5]==0&&m[12]==0&&m[13]==1048576,"quarter turn accounts for non-square dimensions");
    t.magnify_s=8192;t.magnify_t=2048;m=nds_native_model::TextureMatrix(t);
    Check(m[1]==-16384&&m[4]==1024&&m[13]==524288,"resource magnification applies to the complete texture rows");
    t.width=0;nds_native_model::TextureMatrix(t);
    std::puts("native model texture transforms passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
