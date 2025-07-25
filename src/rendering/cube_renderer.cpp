#include "cube_renderer.h"
#include "bgfx/embedded_shader.h"
#include "camera.h"
#include "ecs/entity_manager.h"
#include "glm/trigonometric.hpp"
#include <../ecs/system.h>
#include <bgfx/bgfx.h>
#include <bgfx/defines.h>
#include <cstdio>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/fwd.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iomanip>
#include <iostream>

// shader hell TODO move to a macro
#include "shaders/generated/essl/f_lighting.sc.bin.h"
#include "shaders/generated/essl/v_lighting.sc.bin.h"
#include "shaders/generated/glsl/f_lighting.sc.bin.h"
#include "shaders/generated/glsl/v_lighting.sc.bin.h"
#include "shaders/generated/spirv/f_lighting.sc.bin.h"
#include "shaders/generated/spirv/v_lighting.sc.bin.h"
#if defined(_WIN32)
#include "shaders/generated/dx11/f_lighting.sc.bin.h"
#include "shaders/generated/dx11/v_lighting.sc.bin.h"
#endif //  defined(_WIN32)
#if __APPLE__
#include "shaders/generated/mtl/f_lighting.sc.bin.h"
#include "shaders/generated/mtl/v_lighting.sc.bin.h"
#endif // __APPLE__
#if !defined(_WIN32)
#undef BGFX_EMBEDDED_SHADER_DXBC
#define BGFX_EMBEDDED_SHADER_DXBC(...)
#endif

// no vertex color
struct Vertex {
  float pos[3];
  float normal[3];
  static void init() {
    ms_decl.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .end();
  };

  static bgfx::VertexLayout ms_decl;
};
bgfx::VertexLayout Vertex::ms_decl;

// pre-duplicated vertices, so no need for indices
// each vertex has the same normal (flat shading)
const Vertex vertList[36] = {
    // Front face (0, 0, 1)
    {{-1, -1, 1}, {0, 0, 1}},
    {{1, -1, 1}, {0, 0, 1}},
    {{1, 1, 1}, {0, 0, 1}},
    {{1, 1, 1}, {0, 0, 1}},
    {{-1, 1, 1}, {0, 0, 1}},
    {{-1, -1, 1}, {0, 0, 1}},

    // Back face (0, 0, -1)
    {{1, -1, -1}, {0, 0, -1}},
    {{-1, -1, -1}, {0, 0, -1}},
    {{-1, 1, -1}, {0, 0, -1}},
    {{-1, 1, -1}, {0, 0, -1}},
    {{1, 1, -1}, {0, 0, -1}},
    {{1, -1, -1}, {0, 0, -1}},

    // Left face (-1, 0, 0)
    {{-1, -1, -1}, {-1, 0, 0}},
    {{-1, -1, 1}, {-1, 0, 0}},
    {{-1, 1, 1}, {-1, 0, 0}},
    {{-1, 1, 1}, {-1, 0, 0}},
    {{-1, 1, -1}, {-1, 0, 0}},
    {{-1, -1, -1}, {-1, 0, 0}},

    // Right face (1, 0, 0)
    {{1, -1, 1}, {1, 0, 0}},
    {{1, -1, -1}, {1, 0, 0}},
    {{1, 1, -1}, {1, 0, 0}},
    {{1, 1, -1}, {1, 0, 0}},
    {{1, 1, 1}, {1, 0, 0}},
    {{1, -1, 1}, {1, 0, 0}},

    // Top face (0, 1, 0)
    {{-1, 1, 1}, {0, 1, 0}},
    {{1, 1, 1}, {0, 1, 0}},
    {{1, 1, -1}, {0, 1, 0}},
    {{1, 1, -1}, {0, 1, 0}},
    {{-1, 1, -1}, {0, 1, 0}},
    {{-1, 1, 1}, {0, 1, 0}},

    // Bottom face (0, -1, 0)
    {{-1, -1, -1}, {0, -1, 0}},
    {{1, -1, -1}, {0, -1, 0}},
    {{1, -1, 1}, {0, -1, 0}},
    {{1, -1, 1}, {0, -1, 0}},
    {{-1, -1, 1}, {0, -1, 0}},
    {{-1, -1, -1}, {0, -1, 0}},
};

const bgfx::EmbeddedShader k_vs = BGFX_EMBEDDED_SHADER(v_lighting);
const bgfx::EmbeddedShader k_fs = BGFX_EMBEDDED_SHADER(f_lighting);

void CubeRenderer::Init(App *app) {
  this->app = app;
  Vertex::init();

  cubeVbh = bgfx::createVertexBuffer(bgfx::makeRef(vertList, sizeof(vertList)),
                                     Vertex::ms_decl);
  bgfx::ShaderHandle vsh =
      bgfx::createEmbeddedShader(&k_vs, bgfx::getRendererType(), "v_lighting");

  bgfx::ShaderHandle fsh =
      bgfx::createEmbeddedShader(&k_fs, bgfx::getRendererType(), "f_lighting");

  program = bgfx::createProgram(vsh, fsh, true);

  u_baseCol = bgfx::createUniform("u_baseCol", bgfx::UniformType::Vec4);
  u_lightDir = bgfx::createUniform("u_lightDir", bgfx::UniformType::Vec4);
  bgfx::UniformHandle uniforms[2];

  int width, height;
  app->GetWindowDims(&width, &height);
  // Set view rectangle for 0th view
  bgfx::setViewRect(0, 0, 0, uint16_t(width), uint16_t(height));

  // Clear the view rect
  bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x443355FF, 1.0f,
                     0);

  ECS::Signature signature;
  signature.set(app->coordinator.GetComponentType<Transform>(), true);
  signature.set(app->coordinator.GetComponentType<Cuboid>(), true);
  app->coordinator.SetSystemSignature<CubeRenderer>(signature);

  retriever = app->coordinator.RegisterSystem<CameraRetriever>();
  retriever->app = app;
  ECS::Signature sig;
  sig.set(app->coordinator.GetComponentType<Transform>(), true);
  sig.set(app->coordinator.GetComponentType<Camera>(), true);
  app->coordinator.SetSystemSignature<CameraRetriever>(sig);
}

glm::mat4 ProjectionMatrix(float fovy, float aspect, float near, float far,
                           bool homogeneousDepth) {
  return homogeneousDepth
             ? glm::perspectiveNO(glm::radians(fovy), aspect, near, far)
             : glm::perspectiveZO(glm::radians(fovy), aspect, near, far);
}

void print_mat4(const glm::mat4 &mat) {
  std::cout << std::fixed << std::setprecision(3);
  for (int row = 0; row < 4; ++row) {
    std::cout << "[ ";
    for (int col = 0; col < 4; ++col) {
      std::cout << std::setw(8) << mat[col][row] << " ";
    }
    std::cout << "]\n";
  }
}

void CubeRenderer::Update() {
  ECS::Entity cameraEntity = retriever->GetCamera();
  auto &camera = app->coordinator.GetComponent<Camera>(cameraEntity);
  auto &transform = app->coordinator.GetComponent<Transform>(cameraEntity);

  const glm::vec3 center = {0.0f, 0.0f, 0.0f};
  const glm::vec3 eye = {0.0f, 1.5f, 10.0f};
  int width, height;
  if (!app->GetWindowDims(&width, &height)) {
    std::cout << SDL_GetError() << std::endl;
  }

  glm::mat4 projection = ProjectionMatrix(
      camera.fov, float(width) / float(height), camera.nearPlane,
      camera.farPlane, bgfx::getCaps()->homogeneousDepth);

  glm::mat4 transformCam = glm::translate(glm::mat4(1), transform.position) *
                           glm::mat4_cast(transform.rotation);
  glm::mat4 view = glm::inverse(transformCam);

  bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x443355FF, 1.0f,
                     0);

  bgfx::setViewTransform(0, glm::value_ptr(view), glm::value_ptr(projection));
  bgfx::setViewRect(0, 0, 0, (uint16_t)width, (uint16_t)height);
  if (mEntities.empty()) {
    // submit empty primitive
    bgfx::touch(0);
  }
  const float lightDir[4] = {0.0f, 1.0f, 0.75f, 0.0f};
  bgfx::setUniform(u_lightDir, lightDir);

  for (auto const &entity : mEntities) {
    auto &transform = app->coordinator.GetComponent<Transform>(entity);
    auto &cuboid = app->coordinator.GetComponent<Cuboid>(entity);
    float baseCol[4] = {cuboid.color.x, cuboid.color.y, cuboid.color.z, 1.0f};
    bgfx::setUniform(u_baseCol, baseCol);

    glm::mat4 transformMat = glm::translate(glm::mat4(1), transform.position) *
                             glm::mat4_cast(transform.rotation) *
                             // scaling a cuboid from (-1,-1,-1) by its
                             // halfExtents gives the correct extents
                             glm::scale(glm::mat4(1), cuboid.halfExtents);
    bgfx::setTransform(glm::value_ptr(transformMat));
    bgfx::setVertexBuffer(0, cubeVbh);
    bgfx::setState(BGFX_STATE_DEFAULT);
    bgfx::submit(0, program);
  }
}

ECS::Entity CameraRetriever::GetCamera() {
  for (auto &entity : mEntities) {
    return entity;
  }
  std::cout << "No camera found!" << std::endl;
  std::cout << "Creating default camera" << std::endl;
  ECS::Entity camera = app->coordinator.CreateEntity();
  app->coordinator.AddComponent(camera,
                                Transform{
                                    .position = glm::vec3(0.0, 1.5, 10.0),
                                    .rotation = glm::identity<glm::quat>(),
                                });
  app->coordinator.AddComponent(camera, Camera{
                                            .fov = 60.0f,
                                            .moveSpeed = 5.0f,
                                            .nearPlane = 0.1f,
                                            .farPlane = 0.1f,
                                        });
  return camera;
}

CubeRenderer::~CubeRenderer() {
  bgfx::destroy(program);
  bgfx::destroy(cubeVbh);
  bgfx::destroy(u_baseCol);
  bgfx::destroy(u_lightDir);
  std::cout << "destroyed" << std::endl;
}
