#include "../../zero_rom/zero2np/src/ingame/loading/loading.c"
#undef NDEBUG
#include <cassert>
static bool ready;
static int requests;
u_char GetLanguage(void) { return 0; }
int FileLoadIsEnd2(int, void *) { return ready; }
void BackGroundLoadReq(void) { ++requests; loading_tex_addr = reinterpret_cast<void *>(1); }
int main()
{
    loading_tex_addr = reinterpret_cast<void *>(1);
    loading_ctrl.step = 1;
    LoadingCtrlInit();
    assert(loading_ctrl.step == 0);
    for (int n = 0; n < 8; ++n) LoadingCtrlMain();
    assert(loading_ctrl.step == 0 && requests == 0);
    ready = true;
    LoadingCtrlMain();
    assert(loading_ctrl.step == 1);
    ready = false;
    loading_tex_addr = nullptr;
    LoadingCtrlInit();
    assert(loading_ctrl.step == 0 && requests == 1);
    LoadingCtrlMain();
    assert(loading_ctrl.step == 0);
}
