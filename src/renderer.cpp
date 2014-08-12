// Copyright 2014 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "precompiled.h"
#include "renderer.h"

namespace fpl {

enum {
  kAttributePosition,
  kAttributeNormal,
  kAttributeTexCoord,
  kAttributeColor
};

bool Renderer::Initialize(const vec2i &window_size,
                                             const char *window_title) {
  // Basic SDL initialization, does not actually initialize a Window or OpenGL,
  // typically should not fail.
  if (SDL_Init(SDL_INIT_VIDEO)) {
    last_error_ = std::string("SDL_Init fail: ") + SDL_GetError();
    return false;
  }

  // Force OpenGL ES 2 on mobile.
  #ifdef PLATFORM_MOBILE
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  #else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
  #endif

  // Always double buffer.
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  // Create the window:
  window_ = SDL_CreateWindow(
    window_title,
    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
    window_size.x(), window_size.y(),
    SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN |
      #ifdef PLATFORM_MOBILE
        SDL_WINDOW_BORDERLESS
      #else
        SDL_WINDOW_RESIZABLE
        #ifndef _DEBUG
          //| SDL_WINDOW_FULLSCREEN_DESKTOP
        #endif
      #endif
    );
  if (!window_) {
    last_error_ = std::string("SDL_CreateWindow fail: ") + SDL_GetError();
    return false;
  }
  // Get the size we actually got, which typically is native res for
  // any fullscreen display:
  SDL_GetWindowSize(window_, &window_size_.x(), &window_size_.y());

  // Create the OpenGL context:
  context_ = SDL_GL_CreateContext(window_);
  if (!context_) {
    last_error_ = std::string("SDL_GL_CreateContext fail: ") + SDL_GetError();
    return false;
  }

  // Enable Vsync on desktop
  #ifndef PLATFORM_MOBILE
    SDL_GL_SetSwapInterval(1);
  #endif

  #ifndef PLATFORM_MOBILE
  auto exts = (char *)glGetString(GL_EXTENSIONS);

  if (!strstr(exts, "GL_ARB_vertex_buffer_object") ||
      !strstr(exts, "GL_ARB_multitexture") ||
      !strstr(exts, "GL_ARB_vertex_program") ||
      !strstr(exts, "GL_ARB_fragment_program")) {
    last_error_ = "missing GL extensions";
    return false;
  }

  #endif

  #if !defined(PLATFORM_MOBILE) && !defined(__APPLE__)
  #define GLEXT(type, name) \
    union { \
      void* data; \
      type function; \
    } data_function_union_##name; \
    data_function_union_##name.data = SDL_GL_GetProcAddress(#name); \
    if (!data_function_union_##name.data) { \
      last_error_ = "could not retrieve GL function pointer " #name; \
      return false; \
    } \
    name = data_function_union_##name.function;
      GLBASEEXTS GLEXTS
  #undef GLEXT
  #endif

  return true;
}

void Renderer::AdvanceFrame(bool minimized) {
  if (minimized) {
    // Save some cpu / battery:
    SDL_Delay(10);
  } else {
    SDL_GL_SwapWindow(window_);
  }
  glViewport(0, 0, window_size_.x(), window_size_.y());
}

void Renderer::ShutDown() {
  if (context_) {
    SDL_GL_DeleteContext(context_);
    context_ = nullptr;
  }
  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
}

void Renderer::ClearFrameBuffer(const vec4 &color) {
    glClearColor(color.x(), color.y(), color.z(), color.w());
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}


GLuint Renderer::CompileShader(GLenum stage, GLuint program,
                               const GLchar *source) {
  std::string platform_source =
  #ifdef PLATFORM_MOBILE
    "#ifdef GL_ES\nprecision highp float;\n#endif\n";
  #else
    "#version 120\n";
  #endif
  platform_source += source;
  const char *platform_source_ptr = platform_source.c_str();
  auto shader_obj = glCreateShader(stage);
  glShaderSource(shader_obj, 1, &platform_source_ptr, nullptr);
  glCompileShader(shader_obj);
  GLint success;
  glGetShaderiv(shader_obj, GL_COMPILE_STATUS, &success);
  if (success) {
    glAttachShader(program, shader_obj);
    return shader_obj;
  } else {
    GLint length = 0;
    glGetShaderiv(shader_obj, GL_INFO_LOG_LENGTH, &length);
    last_error_.assign(length, '\0');
    glGetShaderInfoLog(shader_obj, length, &length, &last_error_[0]);
    glDeleteShader(shader_obj);
    return 0;
  }
}

Shader *Renderer::CompileAndLinkShader(const char *vs_source,
                                       const char *ps_source) {
  auto program = glCreateProgram();
  auto vs = CompileShader(GL_VERTEX_SHADER, program, vs_source);
  if (vs) {
    auto ps = CompileShader(GL_FRAGMENT_SHADER, program, ps_source);
    if (ps) {
      glBindAttribLocation(program, kAttributePosition, "aPosition");
      glBindAttribLocation(program, kAttributeNormal,   "aNormal");
      glBindAttribLocation(program, kAttributeTexCoord, "aTexCoord");
      glBindAttribLocation(program, kAttributeColor,    "aColor");
      glLinkProgram(program);
      GLint status;
      glGetProgramiv(program, GL_LINK_STATUS, &status);
      if (status == GL_TRUE) {
        auto shader = new Shader(program, vs, ps);
        shader->Initialize();
        glUseProgram(program);
        return shader;
      }
      GLint length = 0;
      glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
      last_error_.assign(length, '\0');
      glGetProgramInfoLog(program, length, &length, &last_error_[0]);
      glDeleteShader(ps);
    }
    glDeleteShader(vs);
  }
  glDeleteProgram(program);
  return nullptr;
}

GLuint Renderer::CreateTexture(const uint8_t *buffer, const vec2i &size) {
  // TODO: support default args for mipmap/wrap
  GLuint texture_id;
  glGenTextures(1, &texture_id);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture_id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.x(), size.y(), 0, GL_RGBA,
               GL_UNSIGNED_BYTE, buffer);
  glEnable(GL_TEXTURE_2D);
  glEnable(GL_ALPHA);
  glEnable(GL_BLEND);
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glAlphaFunc(GL_GREATER, 0.5);
  glEnable(GL_ALPHA_TEST);
  glGenerateMipmap(GL_TEXTURE_2D);
  return texture_id;
}

GLuint Renderer::CreateTextureFromTGAMemory(const void *tga_buf) {
  struct TGA {
    unsigned char id_len, color_map_type, image_type, color_map_data[5];
    unsigned short x_origin, y_origin, width, height;
    unsigned char bpp, image_descriptor;
  };
  static_assert(sizeof(TGA) == 18,
    "Members of struct TGA need to be packed with no padding.");
  int little_endian = 1;
  if (!*reinterpret_cast<char *>(&little_endian)) {
    return 0; // TODO: endian swap the shorts instead
  }
  auto header = reinterpret_cast<const TGA *>(tga_buf);
  if (header->color_map_type != 0 // no color map
   || header->image_type != 2 // RGB or RGBA only
   || (header->bpp != 32 && header->bpp != 24))
    return 0;
  auto pixels = reinterpret_cast<const unsigned char *>(header + 1);
  pixels += header->id_len;
  int size = header->width * header->height;
  auto rgba = new unsigned char[size * 4];
  int start_y, end_y, y_direction;
  if (header->image_descriptor & 0x20)  // y is flipped
  {
      start_y = header->height - 1;
      end_y = -1;
      y_direction = -1;
  } else {    // y is not flipped.
      start_y = 0;
      end_y = header->height;
      y_direction = 1;
  }
  for (int y = start_y; y != end_y; y += y_direction) {
    for (int x = 0; x < header->width; x++) {
      auto p = rgba + (y * header->width + x) * 4;
      p[2] = *pixels++;    // BGR -> RGB
      p[1] = *pixels++;
      p[0] = *pixels++;
      p[3] = header->bpp == 32 ? *pixels++ : 255;
    }
  }
  auto id = CreateTexture(rgba, vec2i(header->width, header->height));
  delete[] rgba;
  return id;
}

void Mesh::SetAttributes(GLuint vbo, const Attribute *attributes, int stride,
                         const char *buffer) {
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  size_t offset = 0;
  for (;;) {
    switch (*attributes++) {
      case kPosition3f:
        glEnableVertexAttribArray(kAttributePosition);
        glVertexAttribPointer(kAttributePosition, 3, GL_FLOAT, false,
                              stride, buffer + offset);
        offset += 3 * sizeof(float);
        break;
      case kNormal3f:
        glEnableVertexAttribArray(kAttributeNormal);
        glVertexAttribPointer(kAttributeNormal, 3, GL_FLOAT, false,
                              stride, buffer + offset);
        offset += 3 * sizeof(float);
        break;
      case kTexCoord2f:
        glEnableVertexAttribArray(kAttributeTexCoord);
        glVertexAttribPointer(kAttributeTexCoord, 2, GL_FLOAT, false,
                              stride, buffer + offset);
        offset += 2 * sizeof(float);
        break;
      case kColor4ub:
        glEnableVertexAttribArray(kAttributeColor);
        glVertexAttribPointer(kAttributeColor, 4, GL_UNSIGNED_BYTE, true,
                              stride, buffer + offset);
        offset += 4;
        break;
      case kEND:
        return;
    }
  }
}

void Mesh::UnSetAttributes(const Attribute *attributes) {
  for (;;) {
    switch (*attributes++) {
      case kPosition3f:
        glDisableVertexAttribArray(kAttributePosition);
        break;
      case kNormal3f:
        glDisableVertexAttribArray(kAttributeNormal);
        break;
      case kTexCoord2f:
        glDisableVertexAttribArray(kAttributeTexCoord);
        break;
      case kColor4ub:
        glDisableVertexAttribArray(kAttributeColor);
        break;
      case kEND:
        return;
    }
  }
}

void Shader::Initialize() {
  // Look up variables that are standard, but still optionally present in a
  // shader.
  uniform_model_view_projection_ = glGetUniformLocation(
                                        program_, "model_view_projection");

  uniform_color_ = glGetUniformLocation(program_, "color");

  uniform_texture_unit_0 = glGetUniformLocation(program_, "texture_unit_0");
  if (uniform_texture_unit_0 >= 0) glUniform1i(uniform_texture_unit_0, 0);
}

void Shader::Set(const Renderer &renderer) const {
  glUseProgram(program_);
  if (uniform_model_view_projection_ >= 0)
    glUniformMatrix4fv(uniform_model_view_projection_, 1, false,
                       &renderer.camera.model_view_projection_[0]);
  if (uniform_color_ >= 0) glUniform4fv(uniform_color_, 1, &renderer.color[0]);
}

Mesh::Mesh(const void *vertex_data, int count, int vertex_size,
           const Attribute *format)
    : vertex_size_(vertex_size), format_(format) {
  glGenBuffers(1, &vbo_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, count * vertex_size, vertex_data,
               GL_STATIC_DRAW);
}

Mesh::~Mesh() {
  glDeleteBuffers(1, &vbo_);
  for (auto it = indices_.begin(); it != indices_.end(); ++it) {
    glDeleteBuffers(1, &it->ibo);
  }
}

void Mesh::AddIndices(const int *index_data, int count, Material *mat) {
  indices_.push_back(Indices());
  auto &idxs = indices_.back();
  idxs.count = count;
  glGenBuffers(1, &idxs.ibo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, idxs.ibo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(int), index_data,
               GL_STATIC_DRAW);
  idxs.mat = mat;
}

void Mesh::Render(Renderer &renderer) {
  SetAttributes(vbo_, format_, vertex_size_, nullptr);
  for (auto it = indices_.begin(); it != indices_.end(); ++it) {
    it->mat->Set(renderer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, it->ibo);
    glDrawElements(GL_TRIANGLES, it->count, GL_UNSIGNED_INT, 0);
  }
  UnSetAttributes(format_);
}

void Mesh::RenderArray(GLenum primitive, int index_count,
                       const Attribute *format, int vertex_size,
                       const char *vertices, const int *indices) {
  SetAttributes(0, format, vertex_size, vertices);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glDrawElements(primitive, index_count, GL_UNSIGNED_INT, indices);
  UnSetAttributes(format);
}

}  // namespace fpl


#if !defined(PLATFORM_MOBILE) && !defined(__APPLE__)
  #define GLEXT(type, name) type name = nullptr;
    GLBASEEXTS GLEXTS
  #undef GLEXT
#endif
