#include "application.h"

#include <stdexcept>

#include "iostream"

namespace {
void GetHardwareAdapter(IDXGIFactory1 *factory,
                        IDXGIAdapter1 **hardware_adapter,
                        bool request_high_performance = false) {
  *hardware_adapter = nullptr;
  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<IDXGIFactory6> factory6;

  if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory6)))) {
    for (uint32_t adapter_index = 0;
         DXGI_ERROR_NOT_FOUND != factory6->EnumAdapterByGpuPreference(
                                     adapter_index,
                                     request_high_performance
                                         ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE
                                         : DXGI_GPU_PREFERENCE_UNSPECIFIED,
                                     IID_PPV_ARGS(&adapter));
         ++adapter_index) {
      DXGI_ADAPTER_DESC1 adapter_desc;
      adapter->GetDesc1(&adapter_desc);
      if (adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
        continue;
      }
      if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                      _uuidof(ID3D12Device), nullptr))) {
        break;
      }
    }
  }

  if (adapter.Get() == nullptr) {
    for (uint32_t adapter_index = 0;
         DXGI_ERROR_NOT_FOUND !=
         factory->EnumAdapters1(adapter_index, &adapter);
         ++adapter_index) {
      DXGI_ADAPTER_DESC1 adapter_desc;
      adapter->GetDesc1(&adapter_desc);
      if (adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
        continue;
      }
      if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                      _uuidof(ID3D12Device), nullptr))) {
        break;
      }
    }
  }

  *hardware_adapter = adapter.Detach();
}

std::string WStringToString(const std::wstring &wstr) {
  // Convert using WideCharToMultiByte function
  int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(),
                                        nullptr, 0, nullptr, nullptr);
  std::string str_to(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str_to[0],
                      size_needed, nullptr, nullptr);
  return str_to;
}

std::string DataSizeToStringNotation(size_t sz) {
  // Convert the size to human readable notation, from B to TB, reserve 2 digits
  // after the decimal point
  std::string notation;
  if (sz < 1024) {
    notation = std::to_string(sz) + " B";
  } else if (sz < 1024 * 1024) {
    notation = std::to_string(sz / 1024.0).substr(0, 6) + " KB";
  } else if (sz < 1024 * 1024 * 1024) {
    notation = std::to_string(sz / 1024.0 / 1024.0).substr(0, 6) + " MB";
  } else {
    notation =
        std::to_string(sz / 1024.0 / 1024.0 / 1024.0).substr(0, 6) + " GB";
  }
  return notation;
}
}  // namespace

Application::Application(const std::string_view &title,
                         uint32_t width,
                         uint32_t height) {
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  window_ = glfwCreateWindow(width, height, title.data(), nullptr, nullptr);

  // Set Scissor Rect and Viewport
  scissor_rect_.left = 0;
  scissor_rect_.top = 0;
  scissor_rect_.right = width;
  scissor_rect_.bottom = height;
  viewport_.TopLeftX = 0;
  viewport_.TopLeftY = 0;
  viewport_.Width = static_cast<float>(width);
  viewport_.Height = static_cast<float>(height);
  viewport_.MinDepth = 0.0f;
  viewport_.MaxDepth = 1.0f;

  // Set auto refill on Framebuffer resize
  glfwSetFramebufferSizeCallback(
      window_, [](GLFWwindow *window, int width, int height) {
        auto app =
            reinterpret_cast<Application *>(glfwGetWindowUserPointer(window));
        app->scissor_rect_.right = width;
        app->scissor_rect_.bottom = height;
        app->viewport_.Width = static_cast<float>(width);
        app->viewport_.Height = static_cast<float>(height);

        // Recreate swap chain
        app->WaitForPreviousFrame();
        app->swap_chain_.Reset();
        // Reset render target views
        for (int i = 0; i < kFrameCount; i++) {
          app->render_targets_[i].Reset();
        }
        app->BuildSwapchain(width, height);
      });

  // Set user pointer to this class
  glfwSetWindowUserPointer(window_, this);
}

Application::~Application() {
  glfwDestroyWindow(window_);
  glfwTerminate();
}

void Application::Run() {
  OnInitialize();
  while (!glfwWindowShouldClose(window_)) {
    OnUpdate();
    OnRender();
    glfwPollEvents();
  }
  OnClose();
}

void Application::OnInitialize() {
  LoadPipeline();
  LoadAssets();
}

void Application::OnUpdate() {
}

void Application::OnRender() {
  PopulateCommandList();

  // Execute the command list
  ID3D12CommandList *command_lists[] = {command_list_.Get()};
  command_queue_->ExecuteCommandLists(_countof(command_lists), command_lists);

  // Present the frame
  if (FAILED(swap_chain_->Present(1, 0))) {
    throw std::runtime_error("Failed to present the frame");
  }

  WaitForPreviousFrame();
}

void Application::OnClose() {
}

void Application::LoadPipeline() {
  uint32_t dxgi_factory_flags = 0;
#ifdef _DEBUG
  ComPtr<ID3D12Debug> debug_controller;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))) {
    debug_controller->EnableDebugLayer();
    dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
  }
#endif

  if (FAILED(CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&factory_)))) {
    throw std::runtime_error("Failed to create DXGI factory");
  }

  ComPtr<IDXGIAdapter1> hardware_adapter;
  GetHardwareAdapter(factory_.Get(), &hardware_adapter);

  if (FAILED(D3D12CreateDevice(hardware_adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                               IID_PPV_ARGS(&device_)))) {
    throw std::runtime_error("Failed to create D3D12 device");
  }

  // Print Basic information of the adaptor
  DXGI_ADAPTER_DESC1 adapter_desc;
  hardware_adapter->GetDesc1(&adapter_desc);
  std::cout << "Selected Device: " << WStringToString(adapter_desc.Description)
            << std::endl;

  // Create command queue
  D3D12_COMMAND_QUEUE_DESC command_queue_desc = {};
  command_queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  command_queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (FAILED(device_->CreateCommandQueue(&command_queue_desc,
                                         IID_PPV_ARGS(&command_queue_)))) {
    throw std::runtime_error("Failed to create command queue");
  }

  // Get Window Frame height and width from GLFW
  int window_frame_width, window_frame_height;
  glfwGetFramebufferSize(window_, &window_frame_width, &window_frame_height);

  // Create descriptor heap for render target view
  {
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
    rtv_heap_desc.NumDescriptors = kFrameCount;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device_->CreateDescriptorHeap(&rtv_heap_desc,
                                             IID_PPV_ARGS(&rtv_heap_)))) {
      throw std::runtime_error(
          "Failed to create descriptor heap for render target view");
    }
    rtv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  }

  // Create swap chain
  BuildSwapchain(window_frame_width, window_frame_height);

  // Create commandAllocator
  if (FAILED(device_->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator_)))) {
    throw std::runtime_error("Failed to create command allocator");
  }
}

void Application::LoadAssets() {
  // Create an empty root signature
  {
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc = {};
    root_signature_desc.NumParameters = 0;
    root_signature_desc.pParameters = nullptr;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = nullptr;
    root_signature_desc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    if (FAILED(D3D12SerializeRootSignature(&root_signature_desc,
                                           D3D_ROOT_SIGNATURE_VERSION_1,
                                           &signature, &error))) {
      throw std::runtime_error("Failed to serialize root signature");
    }

    if (FAILED(device_->CreateRootSignature(0, signature->GetBufferPointer(),
                                            signature->GetBufferSize(),
                                            IID_PPV_ARGS(&root_signature_)))) {
      throw std::runtime_error("Failed to create root signature");
    }
  }

  // Create the pipeline state, which includes compiling and loading shaders
  {
    ComPtr<ID3DBlob> vertex_shader;
    ComPtr<ID3DBlob> pixel_shader;
    ComPtr<ID3DBlob> error;
#ifdef _DEBUG
    uint32_t compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    uint32_t compile_flags = 0;
#endif

    if (FAILED(D3DCompileFromFile(L"../../shaders/main.hlsl", nullptr, nullptr,
                                  "VSMain", "vs_5_0", compile_flags, 0,
                                  &vertex_shader, &error))) {
      throw std::runtime_error("Failed to compile vertex shader");
    }

    if (FAILED(D3DCompileFromFile(L"../../shaders/main.hlsl", nullptr, nullptr,
                                  "PSMain", "ps_5_0", compile_flags, 0,
                                  &pixel_shader, &error))) {
      throw std::runtime_error("Failed to compile pixel shader");
    }

    // Define the vertex input layout
    D3D12_INPUT_ELEMENT_DESC input_element_descs[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         static_cast<UINT>(offsetof(Vertex, position)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         static_cast<UINT>(offsetof(Vertex, color)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

    // Describe and create the graphics pipeline state object (PSO)
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.InputLayout = {input_element_descs, _countof(input_element_descs)};
    pso_desc.pRootSignature = root_signature_.Get();
    //        pso_desc.VS = {reinterpret_cast<UINT8
    //        *>(vertex_shader->GetBufferPointer()),
    //        vertex_shader->GetBufferSize()}; pso_desc.PS =
    //        {reinterpret_cast<UINT8 *>(pixel_shader->GetBufferPointer()),
    //        pixel_shader->GetBufferSize()};
    pso_desc.VS = CD3DX12_SHADER_BYTECODE(vertex_shader.Get());
    pso_desc.PS = CD3DX12_SHADER_BYTECODE(pixel_shader.Get());
    pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso_desc.DepthStencilState.DepthEnable = FALSE;
    pso_desc.DepthStencilState.StencilEnable = FALSE;
    pso_desc.SampleMask = UINT_MAX;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.SampleDesc.Count = 1;

    if (FAILED(device_->CreateGraphicsPipelineState(
            &pso_desc, IID_PPV_ARGS(&pipeline_state_)))) {
      throw std::runtime_error("Failed to create graphics pipeline state");
    }
  }

  // Create Command List
  if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                        command_allocator_.Get(), nullptr,
                                        IID_PPV_ARGS(&command_list_)))) {
    throw std::runtime_error("Failed to create command list");
  }

  // Create vertex buffer
  {
    Vertex triangle_vertices[] = {
        {{0.0f, 0.25f * 2, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{0.25f * 2, -0.25f * 2, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{-0.25f * 2, -0.25f * 2, 0.0f}, {0.0f, 0.0f, 1.0f}}};
    const uint32_t vertex_buffer_size = sizeof(triangle_vertices);

    CD3DX12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC buffer_desc =
        CD3DX12_RESOURCE_DESC::Buffer(vertex_buffer_size);

    // Create default heap
    if (FAILED(device_->CreateCommittedResource(
            &heap_properties, D3D12_HEAP_FLAG_NONE, &buffer_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&vertex_buffer_)))) {
      throw std::runtime_error("Failed to create vertex buffer");
    }

    // Create upload heap
    ComPtr<ID3D12Resource> vertex_buffer_upload;
    heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    buffer_desc = CD3DX12_RESOURCE_DESC::Buffer(vertex_buffer_size);
    if (FAILED(device_->CreateCommittedResource(
            &heap_properties, D3D12_HEAP_FLAG_NONE, &buffer_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&vertex_buffer_upload)))) {
      throw std::runtime_error("Failed to create vertex buffer upload");
    }

    // Copy data to the intermediate upload heap and then schedule a copy
    D3D12_SUBRESOURCE_DATA vertex_data = {};
    vertex_data.pData = reinterpret_cast<UINT8 *>(triangle_vertices);
    vertex_data.RowPitch = vertex_buffer_size;
    vertex_data.SlicePitch = vertex_data.RowPitch;

    UpdateSubresources(command_list_.Get(), vertex_buffer_.Get(),
                       vertex_buffer_upload.Get(), 0, 0, 1, &vertex_data);

    // Transition the vertex buffer from copy destination state to vertex buffer
    // state
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        vertex_buffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    command_list_->ResourceBarrier(1, &barrier);

    // Initialize vertex buffer view
    vertex_buffer_view_.BufferLocation = vertex_buffer_->GetGPUVirtualAddress();
    vertex_buffer_view_.StrideInBytes = sizeof(Vertex);
    vertex_buffer_view_.SizeInBytes = vertex_buffer_size;

    // Close the command list and execute it to begin the vertex buffer copy
    // into the default heap
    if (FAILED(command_list_->Close())) {
      throw std::runtime_error("Failed to close command list");
    }
    ID3D12CommandList *command_lists[] = {command_list_.Get()};
    command_queue_->ExecuteCommandLists(_countof(command_lists), command_lists);

    // Create synchronization event and wait for the vertex buffer copy to
    // complete
    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                    IID_PPV_ARGS(&fence_)))) {
      throw std::runtime_error("Failed to create fence");
    }
    fence_value_ = 1;
    fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (fence_event_ == nullptr) {
      throw std::runtime_error("Failed to create fence event");
    }

    WaitForPreviousFrame();
  }
}

void Application::WaitForPreviousFrame() {
  // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
  // This is code implemented as such for simplicity. More advanced samples
  // illustrate how to use fences for efficient resource usage.

  // Signal and increment the fence value.
  const uint64_t fence = fence_value_;
  if (FAILED(command_queue_->Signal(fence_.Get(), fence))) {
    throw std::runtime_error("Failed to signal fence");
  }
  fence_value_++;

  // Wait until the previous frame is finished.
  if (fence_->GetCompletedValue() < fence) {
    if (FAILED(fence_->SetEventOnCompletion(fence, fence_event_))) {
      throw std::runtime_error("Failed to set event on completion");
    }
    WaitForSingleObjectEx(fence_event_, INFINITE, FALSE);
  }

  frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
}

void Application::PopulateCommandList() {
  // Reset the command allocator
  if (FAILED(command_allocator_->Reset())) {
    throw std::runtime_error("Failed to reset command allocator");
  }

  // Reset the command list
  if (FAILED(command_list_->Reset(command_allocator_.Get(),
                                  pipeline_state_.Get()))) {
    throw std::runtime_error("Failed to reset command list");
  }

  // Set necessary state
  command_list_->SetGraphicsRootSignature(root_signature_.Get());
  command_list_->RSSetViewports(1, &viewport_);
  command_list_->RSSetScissorRects(1, &scissor_rect_);

  // Indicate that the back buffer will be used as a render target
  CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
      render_targets_[frame_index_].Get(), D3D12_RESOURCE_STATE_PRESENT,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  command_list_->ResourceBarrier(1, &barrier);

  CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(
      rtv_heap_->GetCPUDescriptorHandleForHeapStart(), frame_index_,
      rtv_descriptor_size_);
  command_list_->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);

  // Record commands
  const float clear_color[] = {0.0f, 0.2f, 0.4f, 1.0f};
  command_list_->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);
  command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  command_list_->IASetVertexBuffers(0, 1, &vertex_buffer_view_);
  command_list_->DrawInstanced(3, 1, 0, 0);

  // Indicate that the back buffer will now be used to present
  barrier = CD3DX12_RESOURCE_BARRIER::Transition(
      render_targets_[frame_index_].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_PRESENT);
  command_list_->ResourceBarrier(1, &barrier);

  if (FAILED(command_list_->Close())) {
    throw std::runtime_error("Failed to close command list");
  }
}

void Application::BuildSwapchain(int width, int height) {
  DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
  swap_chain_desc.BufferCount = kFrameCount;
  swap_chain_desc.Width = width;
  swap_chain_desc.Height = height;
  swap_chain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swap_chain_desc.SampleDesc.Count = 1;

  ComPtr<IDXGISwapChain1> swap_chain;
  if (FAILED(factory_->CreateSwapChainForHwnd(
          command_queue_.Get(), glfwGetWin32Window(window_), &swap_chain_desc,
          nullptr, nullptr, &swap_chain))) {
    throw std::runtime_error("Failed to create swap chain");
  }

  if (FAILED(factory_->MakeWindowAssociation(glfwGetWin32Window(window_),
                                             DXGI_MWA_NO_ALT_ENTER))) {
    throw std::runtime_error("Failed to make window association");
  }

  if (FAILED(swap_chain.As(&swap_chain_))) {
    throw std::runtime_error("Failed to get swap chain");
  }

  frame_index_ = swap_chain_->GetCurrentBackBufferIndex();

  // Create frame resources
  {
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(
        rtv_heap_->GetCPUDescriptorHandleForHeapStart());
    for (uint32_t i = 0; i < kFrameCount; i++) {
      if (FAILED(
              swap_chain_->GetBuffer(i, IID_PPV_ARGS(&render_targets_[i])))) {
        throw std::runtime_error("Failed to get swap chain buffer");
      }
      device_->CreateRenderTargetView(render_targets_[i].Get(), nullptr,
                                      rtv_handle);
      rtv_handle.Offset(1, rtv_descriptor_size_);
    }
  }
}
