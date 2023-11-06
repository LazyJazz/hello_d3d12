#include "application.h"

int main() {
  // Create window by glfw for d3d12
  Application app("D3D12", 1920, 1080);
  app.Run();
}
