// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "renderer.h"
#include <bit>
#include <numeric>
#include <stdexcept>
namespace cy {
namespace {
GLuint compile(GLenum type, const char* source) {
    GLuint id = glCreateShader(type);
    glShaderSource(id, 1, &source, nullptr);
    glCompileShader(id);
    GLint ok;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char error[4096]{};
        glGetShaderInfoLog(id, sizeof(error), nullptr, error);
        glDeleteShader(id);
        throw std::runtime_error(error);
    }
    return id;
}
const char* vertex = R"(#version 330 core
layout(location=0)in vec3 position;layout(location=4)in vec4 color;layout(location=7)in vec3 scale;layout(location=8)in vec4 quaternion;
uniform mat4 model;out VS{vec3 p;vec4 color;mat3 axes;} data;
void main(){vec4 q=normalize(quaternion);float x=q.x,y=q.y,z=q.z,w=q.w;
 mat3 r=mat3(1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w),2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w),2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y));
 data.p=(model*vec4(position,1)).xyz;data.color=color;data.axes=mat3(model)*r*mat3(scale.x,0,0,0,scale.y,0,0,0,scale.z);gl_Position=vec4(data.p,1);})";
const char* geometry = R"(#version 330 core
layout(points)in;layout(triangle_strip,max_vertices=4)out;
in VS{vec3 p;vec4 color;mat3 axes;} data[];out vec4 C;out vec2 Q;
uniform mat4 vp;uniform vec2 screen;
void main(){vec4 clip=vp*vec4(data[0].p,1);if(clip.w<=.0001||clip.z < -clip.w)return;
 vec3 row0=vec3(vp[0][0],vp[1][0],vp[2][0]),row1=vec3(vp[0][1],vp[1][1],vp[2][1]),row3=vec3(vp[0][3],vp[1][3],vp[2][3]);
 vec3 jx=(row0*clip.w-row3*clip.x)/(clip.w*clip.w)*screen.x*.5;
 vec3 jy=(row1*clip.w-row3*clip.y)/(clip.w*clip.w)*screen.y*.5;
 vec3 a=transpose(data[0].axes)*jx,b=transpose(data[0].axes)*jy;
 float xx=dot(a,a)+.3,xy=dot(a,b),yy=dot(b,b)+.3;
 float l00=sqrt(xx),l10=xy/l00,l11=sqrt(max(yy-l10*l10,.3));
 mat2 L=mat2(l00,l10,0,l11);C=data[0].color;
 for(int i=0;i<4;++i){Q=vec2((i&1)==0?-3:3,(i&2)==0?-3:3);vec2 offset=L*Q*2/screen;
  gl_Position=clip+vec4(offset*clip.w,0,0);EmitVertex();}EndPrimitive();})";
const char* fragment = R"(#version 330 core
in vec4 C;in vec2 Q;out vec4 color;uniform float exposure;
void main(){float a=C.a*exp(-.5*dot(Q,Q));if(a<1/255.)discard;
 vec3 c=max(C.rgb*exp2(exposure),vec3(0));c=clamp((c*(2.51*c+.03))/(c*(2.43*c+.59)+.14),0,1);
 c=mix(12.92*c,1.055*pow(c,vec3(1/2.4))-.055,step(vec3(.0031308),c));color=vec4(c,min(a,.99));})";
} // namespace
void Renderer::createSplats() {
    auto v = compile(GL_VERTEX_SHADER, vertex), g = compile(GL_GEOMETRY_SHADER, geometry),
         f = compile(GL_FRAGMENT_SHADER, fragment);
    splat_program_ = glCreateProgram();
    glAttachShader(splat_program_, v);
    glAttachShader(splat_program_, g);
    glAttachShader(splat_program_, f);
    glLinkProgram(splat_program_);
    glDeleteShader(v);
    glDeleteShader(g);
    glDeleteShader(f);
    GLint ok;
    glGetProgramiv(splat_program_, GL_LINK_STATUS, &ok);
    if (!ok)
        throw std::runtime_error("Programme de splats indisponible");
    splat_vp_ = glGetUniformLocation(splat_program_, "vp");
    splat_model_ = glGetUniformLocation(splat_program_, "model");
    splat_screen_ = glGetUniformLocation(splat_program_, "screen");
    splat_exposure_ = glGetUniformLocation(splat_program_, "exposure");
}
void Renderer::drawSplats(Mesh& mesh, const Instance& instance, const Mat4& vp, int width, int height) {
    Vec3 eye = camera.eye(), forward = normalized(camera.target - eye);
    if (eye.x != mesh.last_eye.x || eye.y != mesh.last_eye.y || eye.z != mesh.last_eye.z ||
        camera.target.x != mesh.last_target.x || camera.target.y != mesh.last_target.y ||
        camera.target.z != mesh.last_target.z ||
        !std::equal(mesh.last_transform.begin(), mesh.last_transform.end(), instance.data.transform)) {
        size_t count = mesh.splat_positions.size();
        mesh.order.resize(count);
        mesh.scratch.resize(count);
        std::iota(mesh.order.begin(), mesh.order.end(), 0u);
        std::vector<uint32_t> keys(count);
        const float* m = instance.data.transform;
        for (size_t i = 0; i < count; ++i) {
            auto p = mesh.splat_positions[i];
            Vec3 world{m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
                       m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
                       m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
            uint32_t bits = std::bit_cast<uint32_t>(dot(world - eye, forward));
            keys[i] = ~(bits ^ (bits & 0x80000000u ? 0xffffffffu : 0x80000000u));
        }
        for (unsigned shift = 0; shift < 32; shift += 8) {
            std::array<size_t, 256> histogram{};
            for (auto i : mesh.order)
                ++histogram[(keys[i] >> shift) & 255];
            size_t sum = 0;
            for (auto& v : histogram) {
                auto n = v;
                v = sum;
                sum += n;
            }
            for (auto i : mesh.order)
                mesh.scratch[histogram[(keys[i] >> shift) & 255]++] = i;
            mesh.order.swap(mesh.scratch);
        }
        glBindVertexArray(mesh.vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(count * 4), mesh.order.data());
        mesh.last_eye = eye;
        mesh.last_target = camera.target;
        std::copy_n(instance.data.transform, 16, mesh.last_transform.begin());
    }
    glUseProgram(splat_program_);
    glUniformMatrix4fv(splat_vp_, 1, GL_FALSE, vp.v);
    glUniformMatrix4fv(splat_model_, 1, GL_FALSE, instance.data.transform);
    glUniform2f(splat_screen_, static_cast<float>(width), static_cast<float>(height));
    glUniform1f(splat_exposure_, exposure);
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glBindVertexArray(mesh.vao);
    glDrawElements(GL_POINTS, mesh.count, GL_UNSIGNED_INT, nullptr);
    glPolygonMode(GL_FRONT_AND_BACK, wire ? GL_LINE : GL_FILL);
}
} // namespace cy
