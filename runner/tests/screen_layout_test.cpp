#undef NDEBUG // Keep the geometry/input assertions active in Release test builds.
#include "screen_layout.h"
#include "../../launcher/nds_screen_layout_settings.h"
#include <cassert>
#include <cstring>

int main() {
    const auto p=nds_screen_placement(NdsScreenLayout::PictureInPicture,448,256);
    assert(p.width==448 && p.height==192 && p.main_screen==0);
    assert(nds_screen_opacity(p,0)==255 && nds_screen_opacity(p,1)==230);
    assert(nds_screen_blend_pixel(0xFFFFFFFFu,0x00000000u,nds_screen_opacity(p,1))==0xFF191919u);
    assert(nds_screen_blend_pixel(0x00000000u,0xFFFFFFFFu,nds_screen_opacity(p,1))==0xFFE6E6E6u);
    assert(nds_screen_blend_pixel(0xFF112233u,0x00ABCDEFu,nds_screen_opacity(p,0))==0xFFABCDEFu);
    assert(p.screens[1].x==346 && p.screens[1].y==114 && p.screens[1].w==96 && p.screens[1].h==72);
    float x{},y{};
    assert(nds_screen_touch_position(p,256,394,150,&x,&y) && x==128 && y==96);
    assert(!nds_screen_touch_position(p,256,345,150,&x,&y));
    assert(!nds_screen_touch_position(p,256,442,150,&x,&y));
    assert(!nds_screen_touch_position(p,256,394,186,&x,&y));
    for (auto corner:{NdsPipCorner::TopLeft,NdsPipCorner::TopRight,NdsPipCorner::BottomLeft,NdsPipCorner::BottomRight}) {
        const auto q=nds_screen_placement(NdsScreenLayout::PictureInPicture,448,256,corner);
        assert(q.screens[1].x==((corner==NdsPipCorner::TopLeft||corner==NdsPipCorner::BottomLeft)?6:346));
        assert(q.screens[1].y==((corner==NdsPipCorner::TopLeft||corner==NdsPipCorner::TopRight)?6:114));
    }
    const auto swapped=nds_screen_placement(NdsScreenLayout::PictureInPicture,256,448,NdsPipCorner::TopLeft,NdsPipMainScreen::Bottom);
    assert(swapped.main_screen==1 && swapped.width==448);
    assert(nds_screen_opacity(swapped,1)==255 && nds_screen_opacity(swapped,0)==230);
    assert(nds_screen_touch_position(swapped,448,224,96,&x,&y) && x==128 && y==96);
    assert(!nds_screen_touch_position(swapped,448,100,20,&x,&y)); // top inset occludes the native bottom LCD
    assert(!nds_screen_touch_position(swapped,448,10,100,&x,&y)); // adaptive margin is not touch input
    const auto wide_inset=nds_screen_placement(NdsScreenLayout::PictureInPicture,256,1024);
    assert(wide_inset.screens[1].x>=0 && wide_inset.screens[1].x+wide_inset.screens[1].w<=256);
    const auto stacked=nds_screen_placement(NdsScreenLayout::Stacked,448,256);
    assert(nds_screen_opacity(stacked,0)==255 && nds_screen_opacity(stacked,1)==255);
    const auto separate=nds_screen_placement(NdsScreenLayout::Separate,448,256);
    assert(nds_screen_opacity(separate,0)==255 && nds_screen_opacity(separate,1)==255);
    assert(nds_screen_touch_position(stacked,256,224,288,&x,&y) && x==128 && y==96);
    assert(!nds_screen_touch_position(stacked,256,224,96,&x,&y));
    assert(std::strcmp(nds_launcher_screen_layout_arg(0),"stacked")==0);
    assert(std::strcmp(nds_launcher_screen_layout_arg(1),"separate")==0);
    assert(std::strcmp(nds_launcher_pip_corner_arg(2),"bottom-right")==0);
    assert(nds_launcher_screen_layout_count({false})==6 && nds_launcher_screen_layout_count()==10);
    for(int index=0;index<10;++index) {
        assert(std::strcmp(nds_launcher_pip_main_arg(index,{false}),"top")==0);
        assert(std::strcmp(nds_launcher_pip_main_arg(index),index>=6?"bottom":"top")==0);
    }
}
