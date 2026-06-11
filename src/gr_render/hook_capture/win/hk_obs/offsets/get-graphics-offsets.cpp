#include <inttypes.h>
#include <stdio.h>
#include <windows.h>
#include "get-graphics-offsets.h"
#include "tc_common_new/log.h"

namespace tc
{
    D3DOffsets GetD3DOffsets() {
        D3DOffsets d3d_offsets;
        d3d_offsets.d3d8 = {0};
        d3d_offsets.d3d9 = {0};
        d3d_offsets.dxgi = {0};
        d3d_offsets.dxgi2 = {0};

        WNDCLASSA wc = {0};
        wc.style = CS_OWNDC;
        wc.hInstance = GetModuleHandleA(NULL);
        wc.lpfnWndProc = (WNDPROC) DefWindowProcA;
        wc.lpszClassName = DUMMY_WNDCLASS;
        SetErrorMode(SEM_FAILCRITICALERRORS);
        if (!RegisterClassA(&wc)) {
            LOGE("------>failed to register '%s'\n", DUMMY_WNDCLASS);
            return d3d_offsets;
        }

        get_d3d9_offsets(&d3d_offsets.d3d9);
        get_d3d8_offsets(&d3d_offsets.d3d8);
        get_dxgi_offsets(&d3d_offsets.dxgi, &d3d_offsets.dxgi2);

        printf("[d3d8]\n");
        printf("present=0x%" PRIx32 "\n", d3d_offsets.d3d8.present);
        printf("[d3d9]\n");
        printf("present=0x%" PRIx32 "\n", d3d_offsets.d3d9.present);
        printf("present_ex=0x%" PRIx32 "\n", d3d_offsets.d3d9.present_ex);
        printf("present_swap=0x%" PRIx32 "\n", d3d_offsets.d3d9.present_swap);
        printf("d3d9_clsoff=0x%" PRIx32 "\n", d3d_offsets.d3d9.d3d9_clsoff);
        printf("is_d3d9ex_clsoff=0x%" PRIx32 "\n", d3d_offsets.d3d9.is_d3d9ex_clsoff);
        printf("[dxgi]\n");
        printf("present=0x%" PRIx32 "\n", d3d_offsets.dxgi.present);
        printf("present1=0x%" PRIx32 "\n", d3d_offsets.dxgi.present1);
        printf("resize=0x%" PRIx32 "\n", d3d_offsets.dxgi.resize);
        printf("release=0x%" PRIx32 "\n", d3d_offsets.dxgi2.release);
        return d3d_offsets;
    }
}

using namespace tc;
int main(int argc, char** argv)
{
    tc::GetD3DOffsets();
    return 0;
}