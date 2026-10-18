#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <array>
#include <string>
#include <vector>
#include <map>
struct TerrainDraw {
  uint64_t id;
  const std::vector<struct Vertex> *vertices;
};
struct Vertex {
  float x, y, z, u, v, r, g, b, hud = 0;
};
struct Camera {
  std::array<float, 16> vp;
  std::array<float, 4> eye;
  std::array<float, 4> environment;
  std::array<float, 4> lavaLight;
  std::array<float, 4> subsurface{};
};
class Renderer {
public:
  GLFWwindow *window{};
  std::string gpu;
  Renderer(bool validation = false, bool hidden = false);
  ~Renderer();
  void reloadSkin(const std::string &path = "player-skin.png");
  void draw(const std::vector<Vertex> &vertices, const Camera &camera,
            const std::vector<TerrainDraw> &terrain = {}, size_t opaqueTerrain = 0);
  size_t uploadedBytes = 0, drawnTerrainVertices = 0, cacheBytes = 0;
  double gpuFrameMs = 0;
  void invalidateTerrain() { resetTerrain = true; }
  void setSceneScale(float value);
  float sceneScale() const { return requestedScale; }

private:
  VkInstance instance{};
  VkSurfaceKHR surface{};
  VkPhysicalDevice physical{};
  VkDevice device{};
  uint32_t family{};
  VkQueue queue{};
  VkSwapchainKHR swapchain{};
  VkExtent2D extent{};
  VkFormat format{};
  std::vector<VkImage> images;
  std::vector<VkImageView> views;
  std::vector<VkFramebuffer> frames;
  VkRenderPass renderPass{}, worldPass{}, overlayPass{};
  VkFramebuffer worldFrame{};
  VkImage worldColour{}, worldDepth{};
  VkImageView worldColourView{}, worldDepthView{};
  VkDeviceMemory worldColourMemory{}, worldDepthMemory{};
  VkExtent2D worldExtent{};
  float requestedScale = 1;
  bool canScale = false;
  VkFilter scaleFilter = VK_FILTER_NEAREST;
  void clearWorldTargets();
  void makeWorldTargets();
  VkPipelineLayout layout{};
  VkPipeline pipeline{};
  VkCommandPool pool{};
  VkCommandBuffer command{};
  VkSemaphore acquired{};
  std::vector<VkSemaphore> rendered;
  VkFence fence{};
  VkBuffer vertexBuffer{};
  VkDeviceMemory vertexMemory{};
  size_t capacity{};
  void *mapped{};
  VkBuffer terrainBuffer{}, terrainStaging{};
  VkDeviceMemory terrainMemory{}, stagingMemory{};
  void *stagingMapped{};
  VkQueryPool timingPool{};
  float timestampPeriod = 0;
  bool timingPending = false;
  VkBuffer packedBuffer{};
  VkDeviceMemory packedMemory{};
  size_t packedCapacity = 0;
  std::vector<std::pair<uint32_t, uint32_t>> packedRanges;
  struct CachedTerrain {
    size_t offset, bytes, count;
    uint64_t seen;
  };
  std::map<uint64_t, CachedTerrain> terrainCache;
  std::vector<std::pair<size_t, size_t>> freeTerrain;
  uint64_t terrainFrame = 0;
  bool resetTerrain = false;
  VkImage texture{}, depth{};
  VkDeviceMemory textureMemory{}, depthMemory{};
  VkImageView textureView{}, depthView{};
  VkSampler sampler{};
  VkDescriptorSetLayout descriptorLayout{};
  VkDescriptorPool descriptorPool{};
  VkDescriptorSet descriptor{};
  uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags flags);
  void buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer &out, VkDeviceMemory &memory,
              VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  void image(uint32_t width, uint32_t height, VkFormat fmt, VkImageUsageFlags usage, VkImage &out,
             VkDeviceMemory &memory, VkImageView &view, VkImageAspectFlags aspect);
  void makeAtlas();
  void makeSwapchain();
  void clearSwapchain();
};
