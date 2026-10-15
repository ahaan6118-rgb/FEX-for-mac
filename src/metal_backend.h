// Metal backend — bridges D3D11 calls to Apple Metal via ObjC runtime
// Uses dlopen + objc_msgSend to avoid Metal.framework header issues
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <vector>
#include <map>

namespace Metal {

// Opaque ObjC id wrapper — prevents accidental misuse
using ObjcId = id;

// Metal pixel format subset matching DXGI formats
enum MTLPixelFormat : uint64_t {
  MTLPixelFormatInvalid       = 0,
  MTLPixelFormatBGRA8Unorm    = 80,
  MTLPixelFormatBGRA8Unorm_sRGB = 81,
  MTLPixelFormatRGBA8Unorm    = 70,
  MTLPixelFormatRGBA8Unorm_sRGB = 71,
  MTLPixelFormatRGBA16Float   = 115,
  MTLPixelFormatRGBA32Float   = 125,
  MTLPixelFormatDepth32Float  = 252,
  MTLPixelFormatDepth24Unorm_Stencil8 = 255, // may not be available on Apple Silicon
  MTLPixelFormatDepth32Float_Stencil8 = 260,
  MTLPixelFormatR32Float      = 28,
  MTLPixelFormatRG32Float     = 63,
};

// Metal resource storage modes
enum MTLStorageMode : uint64_t {
  MTLStorageModeShared  = 0,
  MTLStorageModeManaged = 1,
  MTLStorageModePrivate = 2,
};

// Metal texture types
enum MTLTextureType : uint64_t {
  MTLTextureType2D = 2,
};

// Metal primitive types
enum MTLPrimitiveType : uint64_t {
  MTLPrimitiveTypePoint         = 0,
  MTLPrimitiveTypeLine          = 1,
  MTLPrimitiveTypeLineStrip     = 2,
  MTLPrimitiveTypeTriangle      = 3,
  MTLPrimitiveTypeTriangleStrip = 4,
};

// Metal load/store actions
enum MTLLoadAction : uint64_t {
  MTLLoadActionDontCare = 0,
  MTLLoadActionLoad     = 1,
  MTLLoadActionClear    = 2,
};

enum MTLStoreAction : uint64_t {
  MTLStoreActionDontCare = 0,
  MTLStoreActionStore    = 1,
};

// MTLVertexFormat — NOT the same as MTLPixelFormat
enum MTLVertexFormat : uint64_t {
  MTLVertexFormatFloat  = 28,
  MTLVertexFormatFloat2 = 29,
  MTLVertexFormatFloat3 = 30,
  MTLVertexFormatFloat4 = 31,
};

// Helper to call ObjC methods
static inline ObjcId msg(ObjcId obj, const char* sel) {
  return ((ObjcId(*)(ObjcId, SEL))objc_msgSend)(obj, sel_registerName(sel));
}

static inline ObjcId msg1(ObjcId obj, const char* sel, ObjcId arg) {
  return ((ObjcId(*)(ObjcId, SEL, ObjcId))objc_msgSend)(obj, sel_registerName(sel), arg);
}

static inline ObjcId msg_ptr(ObjcId obj, const char* sel, void* arg) {
  return ((ObjcId(*)(ObjcId, SEL, void*))objc_msgSend)(obj, sel_registerName(sel), arg);
}

static inline void msg_uint(ObjcId obj, const char* sel, uint64_t val) {
  ((void(*)(ObjcId, SEL, uint64_t))objc_msgSend)(obj, sel_registerName(sel), val);
}

static inline uint64_t msg_get_uint(ObjcId obj, const char* sel) {
  return ((uint64_t(*)(ObjcId, SEL))objc_msgSend)(obj, sel_registerName(sel));
}

static inline bool msg_get_bool(ObjcId obj, const char* sel) {
  return ((bool(*)(ObjcId, SEL))objc_msgSend)(obj, sel_registerName(sel));
}

// Metal buffer resource
struct MetalBuffer {
  ObjcId mtl_buffer;
  uint64_t size;
  uint32_t bind_flags;
  void* contents;
};

// Metal texture resource
struct MetalTexture {
  ObjcId mtl_texture;
  uint32_t width;
  uint32_t height;
  MTLPixelFormat format;
  uint32_t bind_flags;
};

// Render target view
struct MetalRTV {
  ObjcId texture;
  MTLPixelFormat format;
};

// Depth stencil view
struct MetalDSV {
  ObjcId texture;
  MTLPixelFormat format;
};

// Shader objects (Metal shader library)
struct MetalShader {
  ObjcId library;
  ObjcId function;
  enum Type { Vertex, Pixel, Compute } type;
};

// Input layout element
struct InputElement {
  uint32_t semantic_index;
  MTLPixelFormat format;
  uint32_t offset;
  uint32_t slot;
  uint32_t stride;
};

// Input layout
struct MetalInputLayout {
  std::vector<InputElement> elements;
  ObjcId vertex_descriptor;
};

// Render pipeline state
struct MetalPipelineState {
  ObjcId pso;
  ObjcId vertex_func;
  ObjcId fragment_func;
};

// ============================================================
// MetalBackend — creates and manages Metal objects
// ============================================================

class MetalBackend {
public:
  bool Init() {
    void* metal_lib = dlopen("/System/Library/Frameworks/Metal.framework/Metal", RTLD_LAZY);
    if (!metal_lib) {
      printf("[Metal] Failed to load Metal.framework\n");
      return false;
    }

    auto MTLCreateSystemDefaultDevice =
      (ObjcId(*)(void))dlsym(metal_lib, "MTLCreateSystemDefaultDevice");
    if (!MTLCreateSystemDefaultDevice) {
      printf("[Metal] MTLCreateSystemDefaultDevice not found\n");
      return false;
    }

    device_ = MTLCreateSystemDefaultDevice();
    if (!device_) {
      printf("[Metal] No Metal device available\n");
      return false;
    }

    // Get device info
    ObjcId name = msg(device_, "name");
    const char* name_str = ((const char*(*)(ObjcId, SEL))objc_msgSend)(
      name, sel_registerName("UTF8String"));
    uint64_t vram = msg_get_uint(device_, "recommendedMaxWorkingSetSize");

    printf("[Metal] Device: %s\n", name_str);
    printf("[Metal] GPU max working set: %llu MB (capacity, not usage)\n",
           (unsigned long long)(vram / (1024*1024)));

    // Create command queue
    cmd_queue_ = msg(device_, "newCommandQueue");
    if (!cmd_queue_) {
      printf("[Metal] Failed to create command queue\n");
      return false;
    }

    initialized_ = true;
    printf("[Metal] Backend initialized successfully\n");
    return true;
  }

  // Create a Metal buffer (maps to ID3D11Buffer)
  MetalBuffer* CreateBuffer(uint64_t size, uint32_t bind_flags, const void* initial_data) {
    if (!initialized_ || buffers_.size() >= 4096) return nullptr;

    ObjcId mtl_buf;
    if (initial_data && size > 0) {
      mtl_buf = ((ObjcId(*)(ObjcId, SEL, const void*, uint64_t, uint64_t))objc_msgSend)(
        device_, sel_registerName("newBufferWithBytes:length:options:"),
        initial_data, size, (uint64_t)MTLStorageModeShared);
    } else {
      mtl_buf = ((ObjcId(*)(ObjcId, SEL, uint64_t, uint64_t))objc_msgSend)(
        device_, sel_registerName("newBufferWithLength:options:"),
        size, (uint64_t)MTLStorageModeShared);
    }
    if (!mtl_buf) return nullptr;

    auto* buf = new (std::nothrow) MetalBuffer;
    if (!buf) return nullptr;
    buf->mtl_buffer = mtl_buf;
    buf->size = size;
    buf->bind_flags = bind_flags;
    buf->contents = ((void*(*)(ObjcId, SEL))objc_msgSend)(mtl_buf, sel_registerName("contents"));

    buffers_.push_back(buf);
    printf("[Metal] Created buffer: %llu bytes (flags=0x%x)\n",
           (unsigned long long)size, bind_flags);
    return buf;
  }

  // Create a Metal texture (maps to ID3D11Texture2D)
  MetalTexture* CreateTexture2D(uint32_t width, uint32_t height,
                                 MTLPixelFormat format, uint32_t bind_flags) {
    if (!initialized_ || textures_.size() >= 4096) return nullptr;

    // Create texture descriptor via ObjC
    ObjcId desc_class = (ObjcId)objc_getClass("MTLTextureDescriptor");
    ObjcId desc = ((ObjcId(*)(ObjcId, SEL, uint64_t, uint64_t, uint64_t, bool))objc_msgSend)(
      desc_class, sel_registerName("texture2DDescriptorWithPixelFormat:width:height:mipmapped:"),
      (uint64_t)format, (uint64_t)width, (uint64_t)height, false);

    if (bind_flags & 0x20) { // D3D11_BIND_RENDER_TARGET
      uint64_t usage = msg_get_uint(desc, "usage");
      usage |= 0x4; // MTLTextureUsageRenderTarget
      msg_uint(desc, "setUsage:", usage);
    }

    ObjcId mtl_tex = msg1(device_, "newTextureWithDescriptor:", desc);
    if (!mtl_tex) return nullptr;

    auto* tex = new (std::nothrow) MetalTexture;
    if (!tex) return nullptr;
    tex->mtl_texture = mtl_tex;
    tex->width = width;
    tex->height = height;
    tex->format = format;
    tex->bind_flags = bind_flags;

    textures_.push_back(tex);
    printf("[Metal] Created texture: %ux%u fmt=%llu\n", width, height, (unsigned long long)format);
    return tex;
  }

  // Create a depth texture for depth-stencil views
  MetalTexture* CreateDepthTexture(uint32_t width, uint32_t height) {
    if (!initialized_ || textures_.size() >= 4096) return nullptr;

    ObjcId desc_class = (ObjcId)objc_getClass("MTLTextureDescriptor");
    ObjcId desc = ((ObjcId(*)(ObjcId, SEL, uint64_t, uint64_t, uint64_t, bool))objc_msgSend)(
      desc_class, sel_registerName("texture2DDescriptorWithPixelFormat:width:height:mipmapped:"),
      (uint64_t)MTLPixelFormatDepth32Float, (uint64_t)width, (uint64_t)height, false);

    msg_uint(desc, "setUsage:", 0x4); // MTLTextureUsageRenderTarget
    msg_uint(desc, "setStorageMode:", (uint64_t)MTLStorageModePrivate);

    ObjcId mtl_tex = msg1(device_, "newTextureWithDescriptor:", desc);
    if (!mtl_tex) return nullptr;

    auto* tex = new (std::nothrow) MetalTexture;
    if (!tex) return nullptr;
    tex->mtl_texture = mtl_tex;
    tex->width = width;
    tex->height = height;
    tex->format = MTLPixelFormatDepth32Float;
    tex->bind_flags = 0x40; // D3D11_BIND_DEPTH_STENCIL

    textures_.push_back(tex);
    printf("[Metal] Created depth texture: %ux%u\n", width, height);
    return tex;
  }

  // Compile a Metal shader from source
  MetalShader* CompileShader(const char* source, const char* entry_point,
                              MetalShader::Type type) {
    if (!initialized_ || shaders_.size() >= 256) return nullptr;

    // Create NSString from source
    ObjcId ns_class = (ObjcId)objc_getClass("NSString");
    ObjcId ns_source = ((ObjcId(*)(ObjcId, SEL, const char*))objc_msgSend)(
      ns_class, sel_registerName("stringWithUTF8String:"), source);

    // Compile library from source
    ObjcId error = nil;
    ObjcId library = ((ObjcId(*)(ObjcId, SEL, ObjcId, ObjcId, ObjcId*))objc_msgSend)(
      device_, sel_registerName("newLibraryWithSource:options:error:"),
      ns_source, nil, &error);

    if (!library || error) {
      if (error) {
        ObjcId desc = msg(error, "localizedDescription");
        const char* err_str = ((const char*(*)(ObjcId, SEL))objc_msgSend)(
          desc, sel_registerName("UTF8String"));
        printf("[Metal] Shader compile error: %s\n", err_str);
      }
      return nullptr;
    }

    // Get function by name
    ObjcId ns_entry = ((ObjcId(*)(ObjcId, SEL, const char*))objc_msgSend)(
      ns_class, sel_registerName("stringWithUTF8String:"), entry_point);
    ObjcId function = msg1(library, "newFunctionWithName:", ns_entry);
    if (!function) {
      printf("[Metal] Function '%s' not found in shader\n", entry_point);
      return nullptr;
    }

    auto* shader = new (std::nothrow) MetalShader;
    if (!shader) return nullptr;
    shader->library = library;
    shader->function = function;
    shader->type = type;

    shaders_.push_back(shader);
    printf("[Metal] Compiled %s shader: %s\n",
           type == MetalShader::Vertex ? "vertex" : "pixel", entry_point);
    fflush(stdout);
    return shader;
  }

  struct VertexAttrib {
    uint32_t index;
    MTLVertexFormat format;
    uint32_t offset;
    uint32_t buffer_index;
  };

  struct VertexLayout {
    uint32_t stride;
    uint32_t buffer_index;
  };

  MetalPipelineState* CreatePipelineState(MetalShader* vs, MetalShader* ps,
                                           MTLPixelFormat color_format,
                                           const VertexAttrib* attribs = nullptr,
                                           uint32_t num_attribs = 0,
                                           const VertexLayout* layouts = nullptr,
                                           uint32_t num_layouts = 0) {
    if (!initialized_ || pipelines_.size() >= 256) return nullptr;

    ObjcId desc_class = (ObjcId)objc_getClass("MTLRenderPipelineDescriptor");
    ObjcId desc = msg(msg(desc_class, "alloc"), "init");

    ((void(*)(ObjcId, SEL, ObjcId))objc_msgSend)(
      desc, sel_registerName("setVertexFunction:"), vs->function);
    ((void(*)(ObjcId, SEL, ObjcId))objc_msgSend)(
      desc, sel_registerName("setFragmentFunction:"), ps->function);

    // Vertex descriptor
    if (attribs && num_attribs > 0) {
      ObjcId vd_class = (ObjcId)objc_getClass("MTLVertexDescriptor");
      ObjcId vd = msg(msg(vd_class, "alloc"), "init");

      ObjcId vd_attrs = msg(vd, "attributes");
      for (uint32_t i = 0; i < num_attribs; i++) {
        ObjcId attr = ((ObjcId(*)(ObjcId, SEL, uint64_t))objc_msgSend)(
          vd_attrs, sel_registerName("objectAtIndexedSubscript:"), (uint64_t)attribs[i].index);
        msg_uint(attr, "setFormat:", (uint64_t)attribs[i].format);
        msg_uint(attr, "setOffset:", (uint64_t)attribs[i].offset);
        msg_uint(attr, "setBufferIndex:", (uint64_t)attribs[i].buffer_index);
      }

      if (layouts && num_layouts > 0) {
        ObjcId vd_layouts = msg(vd, "layouts");
        for (uint32_t i = 0; i < num_layouts; i++) {
          ObjcId layout = ((ObjcId(*)(ObjcId, SEL, uint64_t))objc_msgSend)(
            vd_layouts, sel_registerName("objectAtIndexedSubscript:"),
            (uint64_t)layouts[i].buffer_index);
          msg_uint(layout, "setStride:", (uint64_t)layouts[i].stride);
          msg_uint(layout, "setStepFunction:", (uint64_t)1); // MTLVertexStepFunctionPerVertex
        }
      }

      ((void(*)(ObjcId, SEL, ObjcId))objc_msgSend)(
        desc, sel_registerName("setVertexDescriptor:"), vd);
    }

    // Color attachment format
    ObjcId attachments = msg(desc, "colorAttachments");
    ObjcId attach0 = ((ObjcId(*)(ObjcId, SEL, uint64_t))objc_msgSend)(
      attachments, sel_registerName("objectAtIndexedSubscript:"), (uint64_t)0);
    msg_uint(attach0, "setPixelFormat:", (uint64_t)color_format);

    ObjcId error = nil;
    ObjcId pso = ((ObjcId(*)(ObjcId, SEL, ObjcId, ObjcId*))objc_msgSend)(
      device_, sel_registerName("newRenderPipelineStateWithDescriptor:error:"),
      desc, &error);

    if (!pso || error) {
      if (error) {
        ObjcId err_desc = msg(error, "localizedDescription");
        const char* err_str = ((const char*(*)(ObjcId, SEL))objc_msgSend)(
          err_desc, sel_registerName("UTF8String"));
        printf("[Metal] Pipeline error: %s\n", err_str);
      }
      return nullptr;
    }

    auto* pipeline = new (std::nothrow) MetalPipelineState;
    if (!pipeline) return nullptr;
    pipeline->pso = pso;
    pipeline->vertex_func = vs->function;
    pipeline->fragment_func = ps->function;

    pipelines_.push_back(pipeline);
    printf("[Metal] Created render pipeline state\n");
    return pipeline;
  }

  // Begin a render pass — creates command buffer + render encoder
  bool BeginRenderPass(ObjcId render_target, float clear_r, float clear_g,
                       float clear_b, float clear_a, ObjcId depth_target = nil) {
    if (!initialized_) return false;

    // Create command buffer
    current_cmd_buf_ = msg(cmd_queue_, "commandBuffer");
    if (!current_cmd_buf_) return false;

    // Create render pass descriptor
    ObjcId rpd_class = (ObjcId)objc_getClass("MTLRenderPassDescriptor");
    ObjcId rpd = msg(rpd_class, "renderPassDescriptor");

    // Color attachment 0
    ObjcId color_attachments = msg(rpd, "colorAttachments");
    ObjcId ca0 = ((ObjcId(*)(ObjcId, SEL, uint64_t))objc_msgSend)(
      color_attachments, sel_registerName("objectAtIndexedSubscript:"), (uint64_t)0);
    ((void(*)(ObjcId, SEL, ObjcId))objc_msgSend)(
      ca0, sel_registerName("setTexture:"), render_target);
    msg_uint(ca0, "setLoadAction:", (uint64_t)MTLLoadActionClear);
    msg_uint(ca0, "setStoreAction:", (uint64_t)MTLStoreActionStore);

    // Set clear color — MTLClearColor is a struct {double r,g,b,a}
    struct { double r, g, b, a; } clear_color = {clear_r, clear_g, clear_b, clear_a};
    ((void(*)(ObjcId, SEL, double, double, double, double))objc_msgSend)(
      ca0, sel_registerName("setClearColor:"),
      (double)clear_r, (double)clear_g, (double)clear_b, (double)clear_a);

    // Depth attachment (optional)
    if (depth_target) {
      ObjcId da = msg(rpd, "depthAttachment");
      ((void(*)(ObjcId, SEL, ObjcId))objc_msgSend)(
        da, sel_registerName("setTexture:"), depth_target);
      msg_uint(da, "setLoadAction:", (uint64_t)MTLLoadActionClear);
      msg_uint(da, "setStoreAction:", (uint64_t)MTLStoreActionDontCare);
    }

    // Create render command encoder
    current_encoder_ = msg1(current_cmd_buf_, "renderCommandEncoderWithDescriptor:", rpd);
    if (!current_encoder_) return false;

    render_pass_active_ = true;
    return true;
  }

  // Set pipeline state on the current encoder
  void SetPipelineState(MetalPipelineState* pipeline) {
    if (!render_pass_active_ || !pipeline) return;
    ((void(*)(ObjcId, SEL, ObjcId))objc_msgSend)(
      current_encoder_, sel_registerName("setRenderPipelineState:"), pipeline->pso);
  }

  // Bind vertex buffer
  void SetVertexBuffer(MetalBuffer* buffer, uint64_t offset, uint32_t index) {
    if (!render_pass_active_ || !buffer) return;
    ((void(*)(ObjcId, SEL, ObjcId, uint64_t, uint64_t))objc_msgSend)(
      current_encoder_, sel_registerName("setVertexBuffer:offset:atIndex:"),
      buffer->mtl_buffer, offset, (uint64_t)index);
  }

  // Draw primitives
  void Draw(MTLPrimitiveType type, uint32_t vertex_count, uint32_t start_vertex) {
    if (!render_pass_active_) return;
    ((void(*)(ObjcId, SEL, uint64_t, uint64_t, uint64_t))objc_msgSend)(
      current_encoder_, sel_registerName("drawPrimitives:vertexStart:vertexCount:"),
      (uint64_t)type, (uint64_t)start_vertex, (uint64_t)vertex_count);
  }

  // Draw indexed primitives
  void DrawIndexed(MTLPrimitiveType type, uint32_t index_count,
                   MetalBuffer* index_buffer, uint64_t offset) {
    if (!render_pass_active_ || !index_buffer) return;
    // MTLIndexTypeUInt32 = 1
    ((void(*)(ObjcId, SEL, uint64_t, uint64_t, uint64_t, ObjcId, uint64_t))objc_msgSend)(
      current_encoder_, sel_registerName("drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:"),
      (uint64_t)type, (uint64_t)index_count, (uint64_t)1,
      index_buffer->mtl_buffer, offset);
  }

  // Set viewport
  void SetViewport(float x, float y, float w, float h, float min_depth, float max_depth) {
    if (!render_pass_active_) return;
    // MTLViewport is a struct of 6 doubles (48 bytes). On ARM64, structs > 16 bytes
    // are passed indirectly via pointer. Use objc_msgSend_stret or invoke via NSInvocation.
    // Simplest: use the C function pointer cast with the struct directly.
    struct MTLViewport { double originX, originY, width, height, znear, zfar; };
    MTLViewport vp = {(double)x, (double)y, (double)w, (double)h, (double)min_depth, (double)max_depth};
    ((void(*)(ObjcId, SEL, MTLViewport))objc_msgSend)(
      current_encoder_, sel_registerName("setViewport:"), vp);
  }

  // Set scissor rect
  void SetScissorRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!render_pass_active_) return;
    struct MTLScissorRect { uint64_t x, y, width, height; };
    MTLScissorRect sr = {x, y, w, h};
    ((void(*)(ObjcId, SEL, MTLScissorRect))objc_msgSend)(
      current_encoder_, sel_registerName("setScissorRect:"), sr);
  }

  // End render pass and commit
  void EndRenderPass() {
    if (!render_pass_active_) return;
    msg(current_encoder_, "endEncoding");
    current_encoder_ = nil;
    render_pass_active_ = false;
  }

  // Commit and wait for GPU
  void CommitAndWait() {
    if (!current_cmd_buf_) return;
    msg(current_cmd_buf_, "commit");
    msg(current_cmd_buf_, "waitUntilCompleted");
    current_cmd_buf_ = nil;
  }

  // Commit without waiting (async)
  void Commit() {
    if (!current_cmd_buf_) return;
    msg(current_cmd_buf_, "commit");
    current_cmd_buf_ = nil;
  }

  // Read texture contents to CPU buffer via blit encoder
  bool ReadTexture(MetalTexture* tex, void* dst, size_t dst_size) {
    if (!tex || !dst || dst_size == 0 || !initialized_) return false;
    uint32_t bpp = 4;
    uint64_t bytes_per_row = tex->width * bpp;
    uint64_t total = bytes_per_row * tex->height;
    if (dst_size < total) return false;

    // Create a shared buffer to copy into
    ObjcId staging = ((ObjcId(*)(ObjcId, SEL, uint64_t, uint64_t))objc_msgSend)(
      device_, sel_registerName("newBufferWithLength:options:"),
      total, (uint64_t)MTLStorageModeShared);
    if (!staging) return false;

    ObjcId cmd = msg(cmd_queue_, "commandBuffer");
    if (!cmd) { msg(staging, "release"); return false; }

    ObjcId blit = msg(cmd, "blitCommandEncoder");
    if (!blit) { msg(staging, "release"); return false; }

    // copyFromTexture:... toBuffer:...
    ((void(*)(ObjcId, SEL,
              ObjcId, uint64_t, uint64_t,
              uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
              ObjcId, uint64_t, uint64_t, uint64_t))objc_msgSend)(
      blit,
      sel_registerName("copyFromTexture:sourceSlice:sourceLevel:sourceOrigin:sourceSize:toBuffer:destinationOffset:destinationBytesPerRow:destinationBytesPerImage:"),
      tex->mtl_texture, (uint64_t)0, (uint64_t)0,
      (uint64_t)0, (uint64_t)0, (uint64_t)0,
      (uint64_t)tex->width, (uint64_t)tex->height, (uint64_t)1,
      staging, (uint64_t)0, bytes_per_row, total);

    msg(blit, "endEncoding");
    msg(cmd, "commit");
    msg(cmd, "waitUntilCompleted");

    void* contents = ((void*(*)(ObjcId, SEL))objc_msgSend)(staging, sel_registerName("contents"));
    if (contents) memcpy(dst, contents, total);
    msg(staging, "release");
    return contents != nullptr;
  }

  // Update buffer contents
  bool UpdateBuffer(MetalBuffer* buf, const void* data, size_t size, size_t offset) {
    if (!buf || !data || !buf->contents) return false;
    if (offset + size > buf->size) return false;
    memcpy(static_cast<uint8_t*>(buf->contents) + offset, data, size);
    return true;
  }

  ObjcId GetDevice() const { return device_; }
  ObjcId GetCommandQueue() const { return cmd_queue_; }
  bool IsInitialized() const { return initialized_; }

  ~MetalBackend() {
    if (!initialized_) return;
    for (auto* b : buffers_) {
      if (b && b->mtl_buffer) msg(b->mtl_buffer, "release");
      delete b;
    }
    for (auto* t : textures_) {
      if (t && t->mtl_texture) msg(t->mtl_texture, "release");
      delete t;
    }
    for (auto* s : shaders_) {
      if (s) {
        if (s->function) msg(s->function, "release");
        if (s->library) msg(s->library, "release");
        delete s;
      }
    }
    for (auto* p : pipelines_) {
      if (p && p->pso) msg(p->pso, "release");
      delete p;
    }
    if (cmd_queue_) msg(cmd_queue_, "release");
    if (device_) msg(device_, "release");
  }

private:
  ObjcId device_ = nil;
  ObjcId cmd_queue_ = nil;
  ObjcId current_cmd_buf_ = nil;
  ObjcId current_encoder_ = nil;
  bool initialized_ = false;
  bool render_pass_active_ = false;

  std::vector<MetalBuffer*> buffers_;
  std::vector<MetalTexture*> textures_;
  std::vector<MetalShader*> shaders_;
  std::vector<MetalPipelineState*> pipelines_;
};

} // namespace Metal
