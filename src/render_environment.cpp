// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "renderer.h"
#include <stdexcept>
#include <string>

namespace cy {
namespace {
const char* vertex = R"(#version 330 core
out vec2 UV;
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);UV=p;gl_Position=vec4(p*2-1,0,1);}
)";
// Deterministic HDR studio: no disk I/O, network or asset dependency.
// All convolutions run once per renderer, not once per model or frame.
const char* fragment = R"(#version 330 core
in vec2 UV;out vec4 color;uniform int face,mode;uniform float roughness;
const float PI=3.14159265359;const uint COUNT=128u;
float radical(uint x){x=(x<<16u)|(x>>16u);x=((x&0x55555555u)<<1u)|((x&0xAAAAAAAAu)>>1u);
 x=((x&0x33333333u)<<2u)|((x&0xCCCCCCCCu)>>2u);x=((x&0x0F0F0F0Fu)<<4u)|((x&0xF0F0F0F0u)>>4u);
 x=((x&0x00FF00FFu)<<8u)|((x&0xFF00FF00u)>>8u);return float(x)*2.3283064365386963e-10;}
vec2 sequence(uint i){return vec2(float(i)/float(COUNT),radical(i));}
vec3 direction(){vec2 q=UV*2-1;
 if(face==0)return normalize(vec3(1,-q.y,-q.x));if(face==1)return normalize(vec3(-1,-q.y,q.x));
 if(face==2)return normalize(vec3(q.x,1,q.y));if(face==3)return normalize(vec3(q.x,-1,-q.y));
 if(face==4)return normalize(vec3(q.x,-q.y,1));return normalize(vec3(-q.x,-q.y,-1));}
vec3 studio(vec3 d){
 vec3 c=mix(vec3(.10,.12,.15),vec3(.42,.48,.56),smoothstep(-.35,.75,d.y));
 float key=pow(max(dot(d,normalize(vec3(-.45,.75,.6))),0),44);
 float rim=pow(max(dot(d,normalize(vec3(.8,.3,-.5))),0),65);
 float ceiling=pow(max(d.y,0),12);
 return c+vec3(7,6.6,5.8)*key+vec3(3.8,4.6,6)*rim+vec3(.9)*ceiling;
}
mat3 basis(vec3 n){vec3 up=abs(n.z)<.999?vec3(0,0,1):vec3(1,0,0);
 vec3 t=normalize(cross(up,n));return mat3(t,cross(n,t),n);}
vec3 halfVector(vec2 xi,float r){float a=r*r,phi=2*PI*xi.x;
 float ct=sqrt((1-xi.y)/(1+(a*a-1)*xi.y)),st=sqrt(max(1-ct*ct,0));
 return vec3(cos(phi)*st,sin(phi)*st,ct);}
float geometry(float nv,float r){float a=r*r;return 2*nv/max(nv+sqrt(a*a+(1-a*a)*nv*nv),1e-6);}
void main(){
 if(mode==2){
  float nv=max(UV.x,.0001),r=UV.y;vec3 v=vec3(sqrt(1-nv*nv),0,nv);vec2 sum=vec2(0);
  for(uint i=0u;i<COUNT;++i){vec3 h=halfVector(sequence(i),r),l=normalize(2*dot(v,h)*h-v);
   float nl=max(l.z,0),nh=max(h.z,0),vh=max(dot(v,h),0);
   if(nl>0){float g=geometry(nv,r)*geometry(nl,r)*vh/max(nh*nv,.0001),fc=pow(1-vh,5);
    sum+=vec2((1-fc)*g,fc*g);}}
  color=vec4(sum/float(COUNT),0,1);return;
 }
 vec3 n=direction(),sum=vec3(0);mat3 frame=basis(n);float weight=0;
 if(mode==0&&roughness<.001){color=vec4(studio(n),1);return;}
 for(uint i=0u;i<COUNT;++i){vec2 xi=sequence(i);vec3 l;
  if(mode==1){float phi=2*PI*xi.x,st=sqrt(xi.y);l=frame*vec3(st*cos(phi),st*sin(phi),sqrt(1-xi.y));
   sum+=studio(l);weight+=1;
  }else{vec3 h=frame*halfVector(xi,roughness);l=normalize(2*dot(n,h)*h-n);float nl=max(dot(n,l),0);
   sum+=studio(l)*nl;weight+=nl;}
 }
 color=vec4(sum/max(weight,.0001),1);
})";
GLuint compile(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char error[4096]{};
        glGetShaderInfoLog(shader, sizeof(error), nullptr, error);
        glDeleteShader(shader);
        throw std::runtime_error(error);
    }
    return shader;
}
struct Temporary {
    GLuint program = 0, framebuffer = 0, vao = 0;
    ~Temporary() {
        glDeleteProgram(program);
        glDeleteFramebuffers(1, &framebuffer);
        glDeleteVertexArrays(1, &vao);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE0);
    }
};
} // namespace
void Renderer::createEnvironment() {
    Temporary temp;
    GLuint vs = compile(GL_VERTEX_SHADER, vertex), fs = compile(GL_FRAGMENT_SHADER, fragment);
    temp.program = glCreateProgram();
    glAttachShader(temp.program, vs);
    glAttachShader(temp.program, fs);
    glLinkProgram(temp.program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(temp.program, GL_LINK_STATUS, &ok);
    if (!ok)
        throw std::runtime_error("Echec de l'eclairage PBR");
    glGenFramebuffers(1, &temp.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, temp.framebuffer);
    glGenVertexArrays(1, &temp.vao);
    glBindVertexArray(temp.vao);
    glUseProgram(temp.program);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
    GLint face = glGetUniformLocation(temp.program, "face"),
          kindUniform = glGetUniformLocation(temp.program, "mode"),
          roughness = glGetUniformLocation(temp.program, "roughness");
    auto draw = [&] {
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Cible d'eclairage PBR indisponible");
        glDrawArrays(GL_TRIANGLES, 0, 3);
    };
    auto cube = [&](GLuint& texture, int size, int levels, int kind) {
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
        for (int level = 0; level < levels; ++level)
            for (unsigned f = 0; f < 6; ++f)
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, level, GL_RGB16F, size >> level,
                             size >> level, 0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                        levels == 1 ? GL_LINEAR : GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, levels - 1);
        glUniform1i(kindUniform, kind);
        for (int level = 0; level < levels; ++level) {
            glViewport(0, 0, size >> level, size >> level);
            glUniform1f(roughness, levels > 1 ? static_cast<float>(level) / (levels - 1) : 1.f);
            for (unsigned f = 0; f < 6; ++f) {
                glUniform1i(face, static_cast<GLint>(f));
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, texture, level);
                draw();
            }
        }
    };
    cube(radiance_, 128, 8, 0);
    cube(irradiance_, 32, 1, 1);
    glGenTextures(1, &brdf_);
    glBindTexture(GL_TEXTURE_2D, brdf_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 128, 128, 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glUniform1i(kindUniform, 2);
    glViewport(0, 0, 128, 128);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdf_, 0);
    draw();
    if (glGetError() != GL_NO_ERROR)
        throw std::runtime_error("Erreur GPU pendant l'eclairage PBR");
}
} // namespace cy
