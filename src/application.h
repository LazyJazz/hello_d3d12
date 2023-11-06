#pragma once
#include "D3Dcompiler.h"
#include "GLFW/glfw3.h"
#include "d3d12.h"
#include "d3dx12.h"
#include "dxgi1_6.h"
#include "wrl.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include "GLFW/glfw3native.h"
// Include DirectX Math
#include "DirectXMath.h"
#include "glm/glm.hpp"

using Microsoft::WRL::ComPtr;

struct Vertex {
  glm::vec3 position;
  glm::vec3 color;
};

class Application {
 public:
  Application(const std::string_view &title, uint32_t width, uint32_t height);
  ~Application();
  void Run();

 private:
  void OnInitialize();
  void OnUpdate();
  void OnRender();
  void OnClose();

  void LoadPipeline();
  void LoadAssets();

  void WaitForPreviousFrame();

  void PopulateCommandList();

  void BuildSwapchain(int width, int height);

  static const uint32_t kFrameCount = 2;

  GLFWwindow *window_;
  ComPtr<ID3D12Device> device_;
  ComPtr<ID3D12CommandQueue> command_queue_;
  ComPtr<IDXGISwapChain3> swap_chain_;
  ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  ComPtr<ID3D12Resource> render_targets_[kFrameCount];
  ComPtr<ID3D12CommandAllocator> command_allocator_;
  ComPtr<ID3D12GraphicsCommandList> command_list_;
  ComPtr<ID3D12Fence> fence_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> pipeline_state_;
  ComPtr<ID3D12Resource> vertex_buffer_;
  D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view_;
  ComPtr<ID3D12Resource> index_buffer_;
  ComPtr<IDXGIFactory4> factory_;
  D3D12_INDEX_BUFFER_VIEW index_buffer_view_;
  HANDLE fence_event_;
  uint64_t fence_value_;
  uint32_t frame_index_;
  uint32_t rtv_descriptor_size_;
  bool is_initialized_;
  CD3DX12_VIEWPORT viewport_;
  CD3DX12_RECT scissor_rect_;
};
