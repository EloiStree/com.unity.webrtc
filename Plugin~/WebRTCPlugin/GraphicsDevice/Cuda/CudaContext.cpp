#include "pch.h"

#include "CudaContext.h"

#include <array>

#if defined(SUPPORT_D3D11)
#include <cudaD3D11.h>
#include <wrl/client.h>
#endif

#if defined(SUPPORT_VULKAN)
#include "GraphicsDevice/Vulkan/VulkanUtility.h"
#endif

#if defined(SUPPORT_D3D11)
using namespace Microsoft::WRL;
#endif

namespace unity
{
namespace webrtc
{
static void* s_hModule = nullptr;

CudaContext::CudaContext()
    : m_context(nullptr)
{
}
//---------------------------------------------------------------------------------------------------------------------
CUresult LoadModule()
{
    // dll check
    if (s_hModule == nullptr)
    {
        // dll delay load
#if defined(_WIN32)
        HMODULE module = LoadLibrary(TEXT("nvcuda.dll"));
        if (module == nullptr)
        {
            LogPrint("nvcuda.dll is not found. Please be sure the environment supports CUDA API.");
            return CUDA_ERROR_NOT_FOUND;
        }
        s_hModule = module;
#else
#endif
    }
    return CUDA_SUCCESS;
}
//---------------------------------------------------------------------------------------------------------------------

CUresult CudaContext::FindCudaDevice(const uint8_t* uuid, CUdevice* cuDevice)
{
    CUdevice _cuDevice = 0;
    CUresult result = CUDA_SUCCESS;
    int numDevices = 0;
    result = cuDeviceGetCount(&numDevices);
    if (result != CUDA_SUCCESS)
    {
        return result;
    }
    CUuuid id = {};

    // Loop over the available devices and identify the CUdevice  corresponding to the physical device in use by
    // this Vulkan instance. This is required because there is no other way to match GPUs across API boundaries.
    for (int i = 0; i < numDevices; i++)
    {
        result = cuDeviceGet(&_cuDevice, i);
        if (result != CUDA_SUCCESS)
        {
            return result;
        }
        result = cuDeviceGetUuid(&id, _cuDevice);
        if (result != CUDA_SUCCESS)
        {
            return result;
        }

        if (!std::memcmp(static_cast<const void*>(&id), static_cast<const void*>(uuid), sizeof(CUuuid)))
        {
            if (cuDevice != nullptr)
                *cuDevice = _cuDevice;
            return CUDA_SUCCESS;
        }
    }
    return CUDA_ERROR_NO_DEVICE;
}

CUresult CudaContext::Init(const VkInstance instance, VkPhysicalDevice physicalDevice)
{
    // dll check
    CUresult result = LoadModule();
    if (result != CUDA_SUCCESS)
    {
        return result;
    }

    CUdevice cuDevice = 0;
    result = cuInit(0);
    if (result != CUDA_SUCCESS)
    {
        return result;
    }

    int numDevices = 0;
    result = cuDeviceGetCount(&numDevices);
    if (result != CUDA_SUCCESS)
    {
        return result;
    }

    CUuuid id = {};
    std::array<uint8_t, VK_UUID_SIZE> deviceUUID;
    if (!VulkanUtility::GetPhysicalDeviceUUIDInto(instance, physicalDevice, &deviceUUID))
    {
        return CUDA_ERROR_INVALID_DEVICE;
    }

    // Loop over the available devices and identify the CUdevice  corresponding to the physical device in use by
    // this Vulkan instance. This is required because there is no other way to match GPUs across API boundaries.
    bool foundDevice = false;
    for (int i = 0; i < numDevices; i++)
    {
        cuDeviceGet(&cuDevice, i);

        cuDeviceGetUuid(&id, cuDevice);

        if (!std::memcmp(
                static_cast<const void*>(&id), static_cast<const void*>(deviceUUID.data()), sizeof(CUuuid)))
        {
            foundDevice = true;
            break;
        }
    }

    if (!foundDevice)
    {
        return CUDA_ERROR_NO_DEVICE;
    }

    result = cuCtxCreate(&m_context, 0, cuDevice);
    return result;
}
//---------------------------------------------------------------------------------------------------------------------

#if defined(SUPPORT_D3D11)
CUresult CudaContext::Init(ID3D11Device* device)
{

    // dll check
    CUresult result = LoadModule();
    if (result != CUDA_SUCCESS)
    {
        return result;
    }

    result = cuInit(0);
    if (result != CUDA_SUCCESS)
    {
        return result;
    }
    int numDevices = 0;
    result = cuDeviceGetCount(&numDevices);
    if (result != CUDA_SUCCESS)
    {
        return result;
    }

    ComPtr<IDXGIDevice> pDxgiDevice = nullptr;
    if (device->QueryInterface(IID_PPV_ARGS(&pDxgiDevice)) != S_OK)
    {
        return CUDA_ERROR_NO_DEVICE;
    }
    ComPtr<IDXGIAdapter> pDxgiAdapter = nullptr;
    if (pDxgiDevice->GetAdapter(&pDxgiAdapter) != S_OK)
    {
        return CUDA_ERROR_NO_DEVICE;
    }
    CUdevice dev;
    if (cuD3D11GetDevice(&dev, pDxgiAdapter.Get()) != CUDA_SUCCESS)
    {
        return CUDA_ERROR_NO_DEVICE;
    }

    result = cuCtxCreate(&m_context, 0, dev);
    return result;
}
#endif
//---------------------------------------------------------------------------------------------------------------------

#if defined(SUPPORT_D3D12)
CUresult CudaContext::Init(ID3D12Device* device)
{

    CUresult result = LoadModule();
    if (result != CUDA_SUCCESS)
    {
        return result;
    }

    result = cuInit(0);
    if (result != CUDA_SUCCESS)
    {
        return result;
    }
    int numDevices = 0;
    result = cuDeviceGetCount(&numDevices);
    if (result != CUDA_SUCCESS)
    {
        return result;
    }

    LUID luid = device->GetAdapterLuid();

    CUdevice cuDevice = 0;
    bool deviceFound = false;

    for (int32_t deviceIndex = 0; deviceIndex < numDevices; deviceIndex++)
    {
        result = cuDeviceGet(&cuDevice, deviceIndex);
        if (result != CUDA_SUCCESS)
        {
            return result;
        }
        char luid_[8];
        unsigned int nodeMask;
        result = cuDeviceGetLuid(luid_, &nodeMask, cuDevice);
        if (result != CUDA_SUCCESS)
        {
            return result;
        }
        if (memcmp(&luid.LowPart, luid_, sizeof(luid.LowPart)) == 0 &&
            memcmp(&luid.HighPart, luid_ + sizeof(luid.LowPart), sizeof(luid.HighPart)) == 0)
        {
            deviceFound = true;
            break;
        }
    }

    if (!deviceFound)
        return CUDA_ERROR_NO_DEVICE;
    return cuCtxCreate(&m_context, 0, cuDevice);
}
#endif
//---------------------------------------------------------------------------------------------------------------------

// todo(kazuki):: not supported on windows
#if defined(SUPPORT_OPENGL_UNIFIED) && defined(UNITY_LINUX)
CUresult CudaContext::InitGL()
{

    // dll check
    CUresult result = LoadModule();
    if (result != CUDA_SUCCESS)
    {
        return result;
    }

    result = cuInit(0);
    if (result != CUDA_SUCCESS)
    {
        return result;
    }

    int numDevices;
    result = cuDeviceGetCount(&numDevices);
    if (CUDA_SUCCESS != result)
    {
        return result;
    }
    if (numDevices == 0)
    {
        return CUDA_ERROR_NO_DEVICE;
    }

    // TODO:: check GPU capability
    int cuDevId = 0;
    CUdevice cuDevice = 0;
    result = cuDeviceGet(&cuDevice, cuDevId);
    if (CUDA_SUCCESS != result)
    {
        return result;
    }
    return cuCtxCreate(&m_context, 0, cuDevice);
}
#endif
//---------------------------------------------------------------------------------------------------------------------
CUcontext CudaContext::GetContext() const
{
    RTC_DCHECK(m_context);

    CUcontext current;
    if (cuCtxGetCurrent(&current) != CUDA_SUCCESS)
    {
        throw;
    }
    if (m_context == current)
    {
        return m_context;
    }
    if (cuCtxSetCurrent(m_context) != CUDA_SUCCESS)
    {
        throw;
    }
    return m_context;
}

void CudaContext::Shutdown()
{
    if (nullptr != m_context)
    {
        cuCtxDestroy(m_context);
        m_context = nullptr;
    }
    if (s_hModule)
    {
#if _WIN32
        FreeLibrary((HMODULE)s_hModule);
#else
#endif
        s_hModule = nullptr;
    }
}

} // end namespace webrtc
} // end namespace unity
