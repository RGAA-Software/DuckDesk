#include <Windows.h>
#include <dxgi1_2.h>
#include <gtest/gtest.h>
#include <wrl/client.h>

#include "gpu/gpu_adapter_identity.h"

namespace {

using Microsoft::WRL::ComPtr;

TEST(GpuAdapterIdentity, PhysicalDxgiAdapterRoundTripsThroughStableKey) {
    ComPtr<IDXGIFactory1> factory;
    ASSERT_TRUE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
    bool verified_adapter = false;
    for (UINT adapter_index = 0;; ++adapter_index) {
        ComPtr<IDXGIAdapter1> adapter;
        const HRESULT result = factory->EnumAdapters1(adapter_index, &adapter);
        if (result == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        ASSERT_TRUE(SUCCEEDED(result));
        DXGI_ADAPTER_DESC1 description{};
        ASSERT_TRUE(SUCCEEDED(adapter->GetDesc1(&description)));
        const auto packed_luid = px::gpu::PackAdapterLuid(description.AdapterLuid.LowPart, description.AdapterLuid.HighPart);
        const auto stable_keys = px::gpu::StableKeysForAdapter(packed_luid);
        for (const auto& stable_key : stable_keys) {
            EXPECT_TRUE(stable_key.starts_with("pnp-sha256:"));
            EXPECT_EQ(stable_key.size(), 75);
            EXPECT_TRUE(px::gpu::AdapterMatchesStableKey(packed_luid, stable_key));
            verified_adapter = true;
        }
    }
    EXPECT_TRUE(verified_adapter);
}

}  // namespace
