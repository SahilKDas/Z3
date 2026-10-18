#include "renderer.hpp"
#include "skin.hpp"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <wincodec.h>
#include <windows.h>
static void check(VkResult r) {
  if (r != VK_SUCCESS)
    throw std::runtime_error("Vulkan error " + std::to_string(r));
}
static std::vector<uint32_t> shader(const char *path) {
  auto bytes = embeddedFile(path);
  if (bytes.empty() || bytes.size() % 4)
    throw std::runtime_error("Missing embedded shader");
  std::vector<uint32_t> b(bytes.size() / 4);
  std::memcpy(b.data(), bytes.data(), bytes.size());
  return b;
}
uint32_t Renderer::memoryType(uint32_t bits, VkMemoryPropertyFlags flags) {
  VkPhysicalDeviceMemoryProperties p;
  vkGetPhysicalDeviceMemoryProperties(physical, &p);
  for (uint32_t i = 0; i < p.memoryTypeCount; i++)
    if ((bits & (1u << i)) && (p.memoryTypes[i].propertyFlags & flags) == flags)
      return i;
  throw std::runtime_error("No compatible GPU memory");
}
void Renderer::buffer(VkDeviceSize n, VkBufferUsageFlags usage, VkBuffer &b, VkDeviceMemory &m,
                      VkMemoryPropertyFlags properties) {
  VkBufferCreateInfo c{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  c.size = n;
  c.usage = usage;
  check(vkCreateBuffer(device, &c, nullptr, &b));
  VkMemoryRequirements r;
  vkGetBufferMemoryRequirements(device, b, &r);
  VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  a.allocationSize = r.size;
  a.memoryTypeIndex = memoryType(r.memoryTypeBits, properties);
  check(vkAllocateMemory(device, &a, nullptr, &m));
  check(vkBindBufferMemory(device, b, m, 0));
}
void Renderer::image(uint32_t w, uint32_t h, VkFormat f, VkImageUsageFlags usage, VkImage &im,
                     VkDeviceMemory &mem, VkImageView &view, VkImageAspectFlags aspect) {
  VkImageCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  c.imageType = VK_IMAGE_TYPE_2D;
  c.extent = {w, h, 1};
  c.mipLevels = c.arrayLayers = 1;
  c.format = f;
  c.tiling = VK_IMAGE_TILING_OPTIMAL;
  c.usage = usage;
  c.samples = VK_SAMPLE_COUNT_1_BIT;
  check(vkCreateImage(device, &c, nullptr, &im));
  VkMemoryRequirements r;
  vkGetImageMemoryRequirements(device, im, &r);
  VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  a.allocationSize = r.size;
  a.memoryTypeIndex = memoryType(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  check(vkAllocateMemory(device, &a, nullptr, &mem));
  check(vkBindImageMemory(device, im, mem, 0));
  VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  v.image = im;
  v.viewType = VK_IMAGE_VIEW_TYPE_2D;
  v.format = f;
  v.subresourceRange = {aspect, 0, 1, 0, 1};
  check(vkCreateImageView(device, &v, nullptr, &view));
}
Renderer::Renderer(bool validation, bool hidden) {
  if (!glfwInit() || !glfwVulkanSupported())
    throw std::runtime_error("Vulkan driver and GLFW are required");
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, hidden ? GLFW_FALSE : GLFW_TRUE);
  window = glfwCreateWindow(1120, 700, "Z3 | Vulkan wilderness", nullptr, nullptr);
  if (!window)
    throw std::runtime_error("Window creation failed");
  uint32_t ec;
  auto extensions = glfwGetRequiredInstanceExtensions(&ec);
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "Z3";
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo ic{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ic.pApplicationInfo = &app;
  ic.enabledExtensionCount = ec;
  ic.ppEnabledExtensionNames = extensions;
  const char *layer = "VK_LAYER_KHRONOS_validation";
  if (validation) {
    ic.enabledLayerCount = 1;
    ic.ppEnabledLayerNames = &layer;
  }
  check(vkCreateInstance(&ic, nullptr, &instance));
  check(glfwCreateWindowSurface(instance, window, nullptr, &surface));
  uint32_t count;
  check(vkEnumeratePhysicalDevices(instance, &count, nullptr));
  std::vector<VkPhysicalDevice> devices(count);
  check(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
  int best = -1;
  for (auto p : devices) {
    uint32_t n;
    vkGetPhysicalDeviceQueueFamilyProperties(p, &n, nullptr);
    std::vector<VkQueueFamilyProperties> q(n);
    vkGetPhysicalDeviceQueueFamilyProperties(p, &n, q.data());
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(p, &props);
    for (uint32_t i = 0; i < n; i++) {
      VkBool32 present;
      check(vkGetPhysicalDeviceSurfaceSupportKHR(p, i, surface, &present));
      if (present && (q[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
        int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
        if (score > best) {
          best = score;
          physical = p;
          family = i;
          gpu = props.deviceName;
        }
      }
    }
  }
  if (best < 0)
    throw std::runtime_error("No graphics/present queue");
  float priority = 1;
  VkDeviceQueueCreateInfo qc{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qc.queueFamilyIndex = family;
  qc.queueCount = 1;
  qc.pQueuePriorities = &priority;
  const char *ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
  VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  dc.queueCreateInfoCount = 1;
  dc.pQueueCreateInfos = &qc;
  dc.enabledExtensionCount = 1;
  dc.ppEnabledExtensionNames = &ext;
  VkPhysicalDeviceFeatures available{}, enabled{};
  vkGetPhysicalDeviceFeatures(physical, &available);

  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(physical, &properties);

  timestampPeriod = properties.limits.timestampPeriod;
  dc.pEnabledFeatures = &enabled;
  check(vkCreateDevice(physical, &dc, nullptr, &device));
  if (properties.limits.timestampComputeAndGraphics) {
    VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    query.queryType = VK_QUERY_TYPE_TIMESTAMP;
    query.queryCount = 2;
    check(vkCreateQueryPool(device, &query, nullptr, &timingPool));
  }
  vkGetDeviceQueue(device, family, 0, &queue);
  VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pc.queueFamilyIndex = family;
  pc.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  check(vkCreateCommandPool(device, &pc, nullptr, &pool));
  VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ca.commandPool = pool;
  ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ca.commandBufferCount = 1;
  check(vkAllocateCommandBuffers(device, &ca, &command));
  VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  check(vkCreateSemaphore(device, &sc, nullptr, &acquired));
  VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fc.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  check(vkCreateFence(device, &fc, nullptr, &fence));
  makeAtlas();
  makeSwapchain();
  std::cout << "GPU: " << gpu << std::endl;
}
void Renderer::makeAtlas() {
  const unsigned W = 512, H = 512;
  std::vector<unsigned char> pixels(W * H * 4, 0);
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  IWICImagingFactory *factory{};
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&factory))))
    throw std::runtime_error("WIC unavailable");
  const wchar_t *names[] = {L"block/grass_block_top",
                            L"block/grass_block_side",
                            L"block/dirt",
                            L"block/stone",
                            L"block/sand",
                            L"block/oak_log",
                            L"block/oak_log_top",
                            L"block/oak_leaves",
                            L"block/water_still",
                            L"block/snow",
                            L"item/elytra",
                            L"item/firework_rocket",
                            L"item/cooked_beef",
                            L"item/apple",
                            L"gui/sprites/hud/heart/full",
                            L"gui/sprites/hud/heart/container",
                            L"gui/sprites/hud/food_full",
                            L"gui/sprites/hud/food_empty",
                            L"item/iron_helmet",
                            L"item/iron_chestplate",
                            L"item/iron_leggings",
                            L"item/iron_boots",
                            L"block/oak_planks",
                            L"item/stick",
                            L"block/crafting_table_top",
                            L"block/lava_still",
                            L"block/deepslate"};
  for (int tile = 0; tile < 27; tile++) {
    std::wstring path = tile == 25 ? L"assets/lava.png"
                        : tile == 26
                            ? L"assets/deepslate.png"
                            : L"assets/minecraft/textures/" + std::wstring(names[tile]) + L".png";
    IWICBitmapDecoder *decoder{};
    if (FAILED(resourceDecoder(factory, path, &decoder)))
      throw std::runtime_error("Missing Minecraft texture");
    IWICBitmapFrameDecode *frame{};
    decoder->GetFrame(0, &frame);
    IWICFormatConverter *convert{};
    factory->CreateFormatConverter(&convert);
    convert->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                        WICBitmapPaletteTypeCustom);
    UINT w, h;
    convert->GetSize(&w, &h);
    std::vector<unsigned char> raw(w * h * 4);
    convert->CopyPixels(nullptr, w * 4, UINT(raw.size()), raw.data());
    for (unsigned y = 0; y < 16; y++)
      for (unsigned x = 0; x < 16; x++)
        std::memcpy(&pixels[(y * W + tile * 16 + x) * 4],
                    &raw[((y * std::min(h, 16u) / 16) * w + x * w / 16) * 4], 4);
    convert->Release();
    frame->Release();
    decoder->Release();
  }
  IWICBitmapDecoder *decoder{};
  if (FAILED(resourceDecoder(factory, L"fonts/english.png", &decoder)))
    throw std::runtime_error("Missing Minecraft glyph atlas");
  IWICBitmapFrameDecode *frame{};
  decoder->GetFrame(0, &frame);
  IWICFormatConverter *convert{};
  factory->CreateFormatConverter(&convert);
  convert->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                      WICBitmapPaletteTypeCustom);
  std::vector<unsigned char> glyphs(W * H * 4);
  convert->CopyPixels(nullptr, W * 4, UINT(glyphs.size()), glyphs.data());
  std::memcpy(pixels.data() + 64 * W * 4, glyphs.data() + 64 * W * 4, (H - 64) * W * 4);
  convert->Release();
  frame->Release();
  decoder->Release();
  const wchar_t *buttons[] = {L"button", L"button_highlighted", L"button_disabled"};
  for (int state = 0; state < 3; state++) {
    std::wstring path =
        L"assets/minecraft/textures/gui/sprites/widget/" + std::wstring(buttons[state]) + L".png";
    IWICBitmapDecoder *bd{};
    if (FAILED(resourceDecoder(factory, path, &bd)))
      throw std::runtime_error("Missing Minecraft button texture");
    IWICBitmapFrameDecode *bf{};
    bd->GetFrame(0, &bf);
    IWICFormatConverter *bc{};
    factory->CreateFormatConverter(&bc);
    bc->Initialize(bf, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                   WICBitmapPaletteTypeCustom);
    std::vector<unsigned char> bytes(200 * 20 * 4);
    bc->CopyPixels(nullptr, 200 * 4, UINT(bytes.size()), bytes.data());
    for (int row = 0; row < 20; row++)
      std::memcpy(pixels.data() + ((384 + state * 24 + row) * W) * 4, bytes.data() + row * 200 * 4,
                  200 * 4);
    bc->Release();
    bf->Release();
    bd->Release();
  }
  auto skin = skinPixels(std::filesystem::exists("player-skin.png") ? "player-skin.png"
                                                                    : "fonts/default-skin.png");
  for (int y = 0; y < 64; y++)
    std::memcpy(pixels.data() + ((128 + y) * W) * 4, skin.data() + y * 256, 256);
  factory->Release();
  CoUninitialize();
  pixels[(32 * W) * 4] = pixels[(32 * W) * 4 + 1] = pixels[(32 * W) * 4 + 2] =
      pixels[(32 * W) * 4 + 3] = 255;
  VkBuffer staging;
  VkDeviceMemory sm;
  buffer(pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging, sm);
  void *dst;
  check(vkMapMemory(device, sm, 0, pixels.size(), 0, &dst));
  std::memcpy(dst, pixels.data(), pixels.size());
  vkUnmapMemory(device, sm);
  image(W, H, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, texture, textureMemory,
        textureView, VK_IMAGE_ASPECT_COLOR_BIT);
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  check(vkBeginCommandBuffer(command, &begin));
  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = texture;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       0, 0, nullptr, 0, nullptr, 1, &barrier);
  VkBufferImageCopy copy{};
  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.imageExtent = {W, H, 1};
  vkCmdCopyBufferToImage(command, staging, texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                       &barrier);
  check(vkEndCommandBuffer(command));
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &command;
  check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
  check(vkQueueWaitIdle(queue));
  vkDestroyBuffer(device, staging, nullptr);
  vkFreeMemory(device, sm, nullptr);
  VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  si.magFilter = si.minFilter = VK_FILTER_NEAREST;
  si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  check(vkCreateSampler(device, &si, nullptr, &sampler));
  VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                       VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
  VkDescriptorSetLayoutCreateInfo lc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  lc.bindingCount = 1;
  lc.pBindings = &binding;
  check(vkCreateDescriptorSetLayout(device, &lc, nullptr, &descriptorLayout));
  VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
  VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dp.maxSets = 1;
  dp.poolSizeCount = 1;
  dp.pPoolSizes = &ps;
  check(vkCreateDescriptorPool(device, &dp, nullptr, &descriptorPool));
  VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  da.descriptorPool = descriptorPool;
  da.descriptorSetCount = 1;
  da.pSetLayouts = &descriptorLayout;
  check(vkAllocateDescriptorSets(device, &da, &descriptor));
  VkDescriptorImageInfo ii{sampler, textureView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet wr{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  wr.dstSet = descriptor;
  wr.dstBinding = 0;
  wr.descriptorCount = 1;
  wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  wr.pImageInfo = &ii;
  vkUpdateDescriptorSets(device, 1, &wr, 0, nullptr);
}
void Renderer::makeSwapchain() {
  VkSurfaceCapabilitiesKHR caps;
  check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps));
  uint32_t n;
  check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &n, nullptr));
  std::vector<VkSurfaceFormatKHR> formats(n);
  check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &n, formats.data()));
  auto chosen = formats[0];
  for (auto f : formats)
    if (f.format == VK_FORMAT_B8G8R8A8_UNORM)
      chosen = f;
  format = chosen.format;
  extent = caps.currentExtent;
  if (extent.width == UINT32_MAX) {
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    extent = {uint32_t(w), uint32_t(h)};
  }
  VkSwapchainCreateInfoKHR c{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  c.surface = surface;
  c.minImageCount =
      std::min(caps.minImageCount + 1, caps.maxImageCount ? caps.maxImageCount : UINT32_MAX);
  c.imageFormat = format;
  c.imageColorSpace = chosen.colorSpace;
  c.imageExtent = extent;
  c.imageArrayLayers = 1;
  VkFormatProperties scaling{};
  vkGetPhysicalDeviceFormatProperties(physical, format, &scaling);
  canScale = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) &&
             (scaling.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) &&
             (scaling.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);
  scaleFilter = (scaling.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
                    ? VK_FILTER_LINEAR
                    : VK_FILTER_NEAREST;
  c.imageUsage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (canScale ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0);
  c.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  c.preTransform = caps.currentTransform;
  c.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  // Prefer mailbox so the explicit 60 Hz limiter does not fight FIFO pacing.
  uint32_t presentCount = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &presentCount, nullptr);
  std::vector<VkPresentModeKHR> presentModes(presentCount);
  vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &presentCount, presentModes.data());
  c.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  for (auto mode : presentModes)
    if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
      c.presentMode = mode;
  c.clipped = VK_TRUE;
  check(vkCreateSwapchainKHR(device, &c, nullptr, &swapchain));
  check(vkGetSwapchainImagesKHR(device, swapchain, &n, nullptr));
  images.resize(n);
  check(vkGetSwapchainImagesKHR(device, swapchain, &n, images.data()));
  views.resize(n);
  rendered.resize(n);
  for (uint32_t i = 0; i < n; i++) {
    VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    v.image = images[i];
    v.viewType = VK_IMAGE_VIEW_TYPE_2D;
    v.format = format;
    v.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(vkCreateImageView(device, &v, nullptr, &views[i]));
    VkSemaphoreCreateInfo s{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    check(vkCreateSemaphore(device, &s, nullptr, &rendered[i]));
  }
  image(extent.width, extent.height, VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depth, depthMemory, depthView,
        VK_IMAGE_ASPECT_DEPTH_BIT);
  std::array<VkAttachmentDescription, 2> ad{};
  ad[0].format = format;
  ad[0].samples = VK_SAMPLE_COUNT_1_BIT;
  ad[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  ad[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  ad[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  ad[1].format = VK_FORMAT_D32_SFLOAT;
  ad[1].samples = VK_SAMPLE_COUNT_1_BIT;
  ad[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  ad[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  ad[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
      dep{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &color;
  sub.pDepthStencilAttachment = &dep;
  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = dependency.dstStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rp.attachmentCount = 2;
  rp.pAttachments = ad.data();
  rp.subpassCount = 1;
  rp.pSubpasses = &sub;
  rp.dependencyCount = 1;
  rp.pDependencies = &dependency;
  check(vkCreateRenderPass(device, &rp, nullptr, &renderPass));
  frames.resize(n);
  for (uint32_t i = 0; i < n; i++) {
    VkImageView attachments[] = {views[i], depthView};
    VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb.renderPass = renderPass;
    fb.attachmentCount = 2;
    fb.pAttachments = attachments;
    fb.width = extent.width;
    fb.height = extent.height;
    fb.layers = 1;
    check(vkCreateFramebuffer(device, &fb, nullptr, &frames[i]));
  }
  auto vb = shader("build/world.vert.spv"), fs = shader("build/world.frag.spv");
  VkShaderModule modules[2];
  for (int i = 0; i < 2; i++) {
    auto &bytes = i ? fs : vb;
    VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    sm.codeSize = bytes.size() * 4;
    sm.pCode = bytes.data();
    check(vkCreateShaderModule(device, &sm, nullptr, &modules[i]));
  }
  VkPipelineShaderStageCreateInfo stages[2]{};
  for (int i = 0; i < 2; i++) {
    stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
    stages[i].module = modules[i];
    stages[i].pName = "main";
  }
  VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
  VkVertexInputAttributeDescription attrs[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
                                               {1, 0, VK_FORMAT_R32G32_SFLOAT, 12},
                                               {2, 0, VK_FORMAT_R32G32B32_SFLOAT, 20},
                                               {3, 0, VK_FORMAT_R32_SFLOAT, 32}};
  VkPipelineVertexInputStateCreateInfo vi{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vi.vertexBindingDescriptionCount = 1;
  vi.pVertexBindingDescriptions = &binding;
  vi.vertexAttributeDescriptionCount = 4;
  vi.pVertexAttributeDescriptions = attrs;
  VkPipelineInputAssemblyStateCreateInfo ia{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vs{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vs.viewportCount = vs.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rs{
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1;
  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo ds{
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  ds.depthTestEnable = ds.depthWriteEnable = VK_TRUE;
  ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  VkPipelineColorBlendAttachmentState ba{};
  ba.colorWriteMask = 15;
  ba.blendEnable = VK_TRUE;
  ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  ba.colorBlendOp = VK_BLEND_OP_ADD;
  ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  ba.alphaBlendOp = VK_BLEND_OP_ADD;
  VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  bs.attachmentCount = 1;
  bs.pAttachments = &ba;
  VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dy.dynamicStateCount = 2;
  dy.pDynamicStates = dynamics;
  VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Camera)};
  VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pl.setLayoutCount = 1;
  pl.pSetLayouts = &descriptorLayout;
  pl.pushConstantRangeCount = 1;
  pl.pPushConstantRanges = &push;
  check(vkCreatePipelineLayout(device, &pl, nullptr, &layout));
  VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pi.stageCount = 2;
  pi.pStages = stages;
  pi.pVertexInputState = &vi;
  pi.pInputAssemblyState = &ia;
  pi.pViewportState = &vs;
  pi.pRasterizationState = &rs;
  pi.pMultisampleState = &ms;
  pi.pDepthStencilState = &ds;
  pi.pColorBlendState = &bs;
  pi.pDynamicState = &dy;
  pi.layout = layout;
  pi.renderPass = renderPass;
  check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline));
  for (auto m : modules)
    vkDestroyShaderModule(device, m, nullptr);
}
void Renderer::setSceneScale(float value) { requestedScale = std::clamp(value, .4f, 1.f); }
void Renderer::clearWorldTargets() {
  if (worldFrame)
    vkDestroyFramebuffer(device, worldFrame, nullptr);
  if (worldPass)
    vkDestroyRenderPass(device, worldPass, nullptr);
  if (overlayPass)
    vkDestroyRenderPass(device, overlayPass, nullptr);
  if (worldColourView)
    vkDestroyImageView(device, worldColourView, nullptr);
  if (worldDepthView)
    vkDestroyImageView(device, worldDepthView, nullptr);
  if (worldColour)
    vkDestroyImage(device, worldColour, nullptr);
  if (worldDepth)
    vkDestroyImage(device, worldDepth, nullptr);
  if (worldColourMemory)
    vkFreeMemory(device, worldColourMemory, nullptr);
  if (worldDepthMemory)
    vkFreeMemory(device, worldDepthMemory, nullptr);
  worldFrame = {};
  worldPass = {};
  overlayPass = {};
  worldColourView = {};
  worldDepthView = {};
  worldColour = {};
  worldDepth = {};
  worldColourMemory = {};
  worldDepthMemory = {};
  worldExtent = {};
}
void Renderer::makeWorldTargets() {
  VkExtent2D wanted{std::max(1u, uint32_t(extent.width * requestedScale)),
                    std::max(1u, uint32_t(extent.height * requestedScale))};
  if (worldFrame && worldExtent.width == wanted.width && worldExtent.height == wanted.height)
    return;
  clearWorldTargets();
  worldExtent = wanted;
  image(wanted.width, wanted.height, format,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, worldColour,
        worldColourMemory, worldColourView, VK_IMAGE_ASPECT_COLOR_BIT);
  image(wanted.width, wanted.height, VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, worldDepth, worldDepthMemory, worldDepthView,
        VK_IMAGE_ASPECT_DEPTH_BIT);
  std::array<VkAttachmentDescription, 2> attachments{};
  attachments[0].format = format;
  attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  attachments[1].format = VK_FORMAT_D32_SFLOAT;
  attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  VkAttachmentReference colour{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
      depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &colour;
  sub.pDepthStencilAttachment = &depthRef;
  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = dependency.dstStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  pass.attachmentCount = 2;
  pass.pAttachments = attachments.data();
  pass.subpassCount = 1;
  pass.pSubpasses = &sub;
  pass.dependencyCount = 1;
  pass.pDependencies = &dependency;
  check(vkCreateRenderPass(device, &pass, nullptr, &worldPass));
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  dependency.dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
  check(vkCreateRenderPass(device, &pass, nullptr, &overlayPass));
  VkImageView views[] = {worldColourView, worldDepthView};
  VkFramebufferCreateInfo frame{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  frame.renderPass = worldPass;
  frame.attachmentCount = 2;
  frame.pAttachments = views;
  frame.width = wanted.width;
  frame.height = wanted.height;
  frame.layers = 1;
  check(vkCreateFramebuffer(device, &frame, nullptr, &worldFrame));
}
void Renderer::clearSwapchain() {
  clearWorldTargets();
  for (auto f : frames)
    vkDestroyFramebuffer(device, f, nullptr);
  for (auto v : views)
    vkDestroyImageView(device, v, nullptr);
  for (auto s : rendered)
    vkDestroySemaphore(device, s, nullptr);
  vkDestroyPipeline(device, pipeline, nullptr);
  vkDestroyPipelineLayout(device, layout, nullptr);
  vkDestroyRenderPass(device, renderPass, nullptr);
  vkDestroyImageView(device, depthView, nullptr);
  vkDestroyImage(device, depth, nullptr);
  vkFreeMemory(device, depthMemory, nullptr);
  vkDestroySwapchainKHR(device, swapchain, nullptr);
}
void Renderer::draw(const std::vector<Vertex> &vertices, const Camera &camera,
                    const std::vector<TerrainDraw> &terrain, size_t opaqueTerrain) {
  int w, h;
  glfwGetFramebufferSize(window, &w, &h);
  if (!w || !h)
    return;
  // Keep pumping window events even if the driver temporarily stops completing frames.
  auto fenceResult = vkWaitForFences(device, 1, &fence, VK_TRUE, 100000000);
  if (fenceResult == VK_TIMEOUT)
    return;
  check(fenceResult);
  if (timingPool && timingPending) {
    uint64_t ticks[2]{};
    if (vkGetQueryPoolResults(device, timingPool, 0, 2, sizeof(ticks), ticks, sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
      gpuFrameMs = double(ticks[1] - ticks[0]) * timestampPeriod / 1000000.;
    timingPending = false;
  }
  if (uint32_t(w) != extent.width || uint32_t(h) != extent.height) {
    check(vkDeviceWaitIdle(device));
    clearSwapchain();
    makeSwapchain();
  }
  uint32_t index;
  auto result =
      vkAcquireNextImageKHR(device, swapchain, 100000000, acquired, VK_NULL_HANDLE, &index);
  if (result == VK_TIMEOUT || result == VK_NOT_READY)
    return;
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    check(vkDeviceWaitIdle(device));
    clearSwapchain();
    makeSwapchain();
    return;
  }
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    check(result);
  constexpr size_t arenaSize = 96 * 1024 * 1024, uploadLimit = 3 * 1024 * 1024;
  ++terrainFrame;
  uploadedBytes = 0;
  drawnTerrainVertices = 0;
  if (!terrainBuffer) {
    buffer(arenaSize,
           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
           terrainBuffer, terrainMemory, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    buffer(uploadLimit, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, terrainStaging, stagingMemory);
    check(vkMapMemory(device, stagingMemory, 0, uploadLimit, 0, &stagingMapped));
    freeTerrain = {{0, arenaSize}};
  }
  auto release = [&](auto it) {
    freeTerrain.push_back({it->second.offset, it->second.bytes});
    terrainCache.erase(it);
  };
  if (resetTerrain) {
    terrainCache.clear();
    freeTerrain = {{0, arenaSize}};
    resetTerrain = false;
  }
  for (auto it = terrainCache.begin(); it != terrainCache.end();) {
    if (terrainFrame - it->second.seen > 180) {
      auto old = it++;
      release(old);
    } else
      ++it;
  }
  auto mergeFree = [&] {
    std::sort(freeTerrain.begin(), freeTerrain.end());
    size_t n = 0;
    for (auto part : freeTerrain) {
      if (n && freeTerrain[n - 1].first + freeTerrain[n - 1].second == part.first)
        freeTerrain[n - 1].second += part.second;
      else
        freeTerrain[n++] = part;
    }
    freeTerrain.resize(n);
  };
  mergeFree();
  std::vector<VkBufferCopy> copies;
  std::vector<std::pair<uint32_t, uint32_t>> ranges(terrain.size());
  for (size_t i = 0; i < terrain.size(); ++i) {
    auto &draw = terrain[i];
    if (draw.vertices->empty())
      continue;
    auto entry = terrainCache.find(draw.id);
    if (entry == terrainCache.end()) {
      size_t actual = draw.vertices->size() * sizeof(Vertex),
             aligned = ((actual + 2303) / 2304) * 2304;
      if (uploadedBytes + actual > uploadLimit)
        continue;
      auto allocation = std::find_if(freeTerrain.begin(), freeTerrain.end(),
                                     [&](auto p) { return p.second >= aligned; });
      while (allocation == freeTerrain.end()) {
        auto oldest = terrainCache.end();
        for (auto it = terrainCache.begin(); it != terrainCache.end(); ++it)
          if (it->second.seen != terrainFrame &&
              (oldest == terrainCache.end() || it->second.seen < oldest->second.seen))
            oldest = it;
        if (oldest == terrainCache.end())
          break;
        release(oldest);
        mergeFree();
        allocation = std::find_if(freeTerrain.begin(), freeTerrain.end(),
                                  [&](auto p) { return p.second >= aligned; });
      }
      if (allocation == freeTerrain.end())
        continue;
      size_t offset = allocation->first;
      allocation->first += aligned;
      allocation->second -= aligned;
      std::memcpy(static_cast<char *>(stagingMapped) + uploadedBytes, draw.vertices->data(),
                  actual);
      copies.push_back({uploadedBytes, offset, actual});
      uploadedBytes += actual;
      entry =
          terrainCache
              .emplace(draw.id, CachedTerrain{offset, aligned, draw.vertices->size(), terrainFrame})
              .first;
    }
    entry->second.seen = terrainFrame;
    ranges[i] = {uint32_t(entry->second.offset / sizeof(Vertex)), uint32_t(entry->second.count)};
    drawnTerrainVertices += entry->second.count;
  }
  cacheBytes = 0;
  for (auto &[id, c] : terrainCache)
    cacheBytes += c.bytes;
  size_t worldCount = 0;
  while (worldCount < vertices.size() && vertices[worldCount].hud < .5f)
    ++worldCount;
  bool scaled = canScale && requestedScale < .999f && (!terrain.empty() || worldCount > 0);
  if (scaled)
    makeWorldTargets();
  std::vector<VkBufferCopy> packedCopies;
  size_t packedOpaque = 0, packedTotal = 0;
  for (size_t i = 0; i < ranges.size(); ++i) {
    if (i < opaqueTerrain)
      packedOpaque += ranges[i].second;
    packedTotal += ranges[i].second;
  }
  if (packedTotal && (ranges != packedRanges || !copies.empty())) {
    size_t needed = packedTotal * sizeof(Vertex);
    if (needed > packedCapacity) {
      if (packedBuffer)
        vkDestroyBuffer(device, packedBuffer, nullptr);
      if (packedMemory)
        vkFreeMemory(device, packedMemory, nullptr);
      packedCapacity = std::min(arenaSize, std::max(size_t(4 * 1024 * 1024), needed * 2));
      buffer(packedCapacity, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
             packedBuffer, packedMemory, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }
    size_t offset = 0;
    for (auto range : ranges)
      if (range.second) {
        size_t bytes = range.second * sizeof(Vertex);
        packedCopies.push_back({range.first * sizeof(Vertex), offset, bytes});
        offset += bytes;
      }
    packedRanges = ranges;
  }
  size_t bytes = vertices.size() * sizeof(Vertex);
  if (bytes > capacity || !vertexBuffer) {
    if (mapped)
      vkUnmapMemory(device, vertexMemory);
    if (vertexBuffer)
      vkDestroyBuffer(device, vertexBuffer, nullptr);
    if (vertexMemory)
      vkFreeMemory(device, vertexMemory, nullptr);
    capacity = std::max(bytes * 2, size_t(1024));
    buffer(capacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexBuffer, vertexMemory);
    check(vkMapMemory(device, vertexMemory, 0, capacity, 0, &mapped));
  }
  if (bytes)
    std::memcpy(mapped, vertices.data(), bytes);
  check(vkResetCommandBuffer(command, 0));
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  check(vkBeginCommandBuffer(command, &begin));
  if (!copies.empty()) {
    vkCmdCopyBuffer(command, terrainStaging, terrainBuffer, uint32_t(copies.size()), copies.data());
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = terrainBuffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                         nullptr, 1, &barrier, 0, nullptr);
  }
  if (!packedCopies.empty()) {
    vkCmdCopyBuffer(command, terrainBuffer, packedBuffer, uint32_t(packedCopies.size()),
                    packedCopies.data());
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = packedBuffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr, 1, &barrier, 0,
                         nullptr);
  }
  if (timingPool) {
    vkCmdResetQueryPool(command, timingPool, 0, 2);
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, timingPool, 0);
  }
  VkClearValue clears[2]{};
  const float cloud = camera.environment[1], flash = camera.environment[2];
  const float clearSky[] = {.58f, .76f, .9f}, overcast[] = {.38f, .42f, .48f},
              litSky[] = {.88f, .91f, 1.f};
  for (int i = 0; i < 3; i++) {
    float sky = clearSky[i] * (1 - cloud) + overcast[i] * cloud;
    clears[0].color.float32[i] = sky * (1 - flash) + litSky[i] * flash;
  }
  if (camera.environment[0] > .5f)
    clears[0].color = {{.02f, .18f, .32f, 1}};
  if (camera.subsurface[0] > .5f && camera.environment[0] < .5f)
    clears[0].color = {{.055f, .065f, .075f, 1}};
  clears[0].color.float32[3] = 1;
  clears[1].depthStencil = {1, 0};
  VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  rp.renderPass = scaled ? worldPass : renderPass;
  rp.framebuffer = scaled ? worldFrame : frames[index];
  rp.renderArea.extent = scaled ? worldExtent : extent;
  rp.clearValueCount = 2;
  rp.pClearValues = clears;
  vkCmdBeginRenderPass(command, &rp, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  auto renderExtent = scaled ? worldExtent : extent;
  VkViewport viewport{0, 0, float(renderExtent.width), float(renderExtent.height), 0, 1};
  VkRect2D scissor{{0, 0}, renderExtent};
  vkCmdSetViewport(command, 0, 1, &viewport);
  vkCmdSetScissor(command, 0, 1, &scissor);
  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(command, 0, 1, &vertexBuffer, &offset);
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &descriptor, 0,
                          nullptr);
  vkCmdPushConstants(command, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Camera), &camera);
  if (packedTotal) {
    vkCmdBindVertexBuffers(command, 0, 1, &packedBuffer, &offset);
    if (packedOpaque)
      vkCmdDraw(command, uint32_t(packedOpaque), 1, 0, 0);
    if (packedTotal > packedOpaque)
      vkCmdDraw(command, uint32_t(packedTotal - packedOpaque), 1, uint32_t(packedOpaque), 0);
  }
  vkCmdBindVertexBuffers(command, 0, 1, &vertexBuffer, &offset);
  size_t dynamicCount = scaled ? worldCount : vertices.size();
  if (dynamicCount)
    vkCmdDraw(command, uint32_t(dynamicCount), 1, 0, 0);
  vkCmdEndRenderPass(command);
  if (scaled) {
    VkImageMemoryBarrier source{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    source.oldLayout = source.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    source.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    source.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    source.srcQueueFamilyIndex = source.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    source.image = worldColour;
    source.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &source);
    VkImageMemoryBarrier target{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    target.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    target.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    target.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    target.srcQueueFamilyIndex = target.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    target.image = images[index];
    target.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &target);
    VkImageBlit blit{};
    blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {int32_t(worldExtent.width), int32_t(worldExtent.height), 1};
    blit.dstOffsets[1] = {int32_t(extent.width), int32_t(extent.height), 1};
    vkCmdBlitImage(command, worldColour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, images[index],
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, scaleFilter);
    target.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    target.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    target.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    target.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &target);
    rp.renderPass = overlayPass;
    rp.framebuffer = frames[index];
    rp.renderArea.extent = extent;
    vkCmdBeginRenderPass(command, &rp, VK_SUBPASS_CONTENTS_INLINE);
    viewport.width = float(extent.width);
    viewport.height = float(extent.height);
    scissor.extent = extent;
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindVertexBuffers(command, 0, 1, &vertexBuffer, &offset);
    if (vertices.size() > worldCount)
      vkCmdDraw(command, uint32_t(vertices.size() - worldCount), 1, uint32_t(worldCount), 0);
    vkCmdEndRenderPass(command);
  }
  if (timingPool)
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timingPool, 1);
  check(vkEndCommandBuffer(command));
  check(vkResetFences(device, 1, &fence));
  VkPipelineStageFlags wait =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.waitSemaphoreCount = 1;
  submit.pWaitSemaphores = &acquired;
  submit.pWaitDstStageMask = &wait;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &command;
  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &rendered[index];
  check(vkQueueSubmit(queue, 1, &submit, fence));
  timingPending = timingPool != VK_NULL_HANDLE;
  VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores = &rendered[index];
  present.swapchainCount = 1;
  present.pSwapchains = &swapchain;
  present.pImageIndices = &index;
  result = vkQueuePresentKHR(queue, &present);
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR)
    check(result);
}
Renderer::~Renderer() {
  if (device) {
    vkDeviceWaitIdle(device);
    clearSwapchain();
    if (timingPool)
      vkDestroyQueryPool(device, timingPool, nullptr);
    if (packedBuffer)
      vkDestroyBuffer(device, packedBuffer, nullptr);
    if (packedMemory)
      vkFreeMemory(device, packedMemory, nullptr);

    if (stagingMapped)
      vkUnmapMemory(device, stagingMemory);
    vkDestroyBuffer(device, terrainBuffer, nullptr);
    vkFreeMemory(device, terrainMemory, nullptr);
    vkDestroyBuffer(device, terrainStaging, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
    if (mapped)
      vkUnmapMemory(device, vertexMemory);
    vkDestroyBuffer(device, vertexBuffer, nullptr);
    vkFreeMemory(device, vertexMemory, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
    vkDestroySampler(device, sampler, nullptr);
    vkDestroyImageView(device, textureView, nullptr);
    vkDestroyImage(device, texture, nullptr);
    vkFreeMemory(device, textureMemory, nullptr);
    vkDestroySemaphore(device, acquired, nullptr);
    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, pool, nullptr);
    vkDestroyDevice(device, nullptr);
  }
  vkDestroySurfaceKHR(instance, surface, nullptr);
  vkDestroyInstance(instance, nullptr);
  glfwDestroyWindow(window);
  glfwTerminate();
  RemoveFontResourceExW(L"fonts/Nunito.ttf", FR_PRIVATE, nullptr);
}

void Renderer::reloadSkin(const std::string &path) {
  auto pixels = skinPixels(path);
  check(vkDeviceWaitIdle(device));
  check(vkResetCommandBuffer(command, 0));
  VkBuffer staging{};
  VkDeviceMemory memory{};
  buffer(pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging, memory);
  void *data{};
  check(vkMapMemory(device, memory, 0, pixels.size(), 0, &data));
  std::memcpy(data, pixels.data(), pixels.size());
  vkUnmapMemory(device, memory);
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  check(vkBeginCommandBuffer(command, &begin));
  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = texture;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
  VkBufferImageCopy copy{};
  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.imageOffset = {0, 128, 0};
  copy.imageExtent = {64, 64, 1};
  vkCmdCopyBufferToImage(command, staging, texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                       &barrier);
  check(vkEndCommandBuffer(command));
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &command;
  check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
  check(vkQueueWaitIdle(queue));
  vkDestroyBuffer(device, staging, nullptr);
  vkFreeMemory(device, memory, nullptr);
}
