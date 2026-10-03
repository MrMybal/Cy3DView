// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "renderer.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>
#include <cstring>
#include <fstream>
#include <stdexcept>
namespace cy {
namespace {
bool writePNG(const fs::path& file, int width, int height, const std::vector<uint8_t>& pixels) {
    std::ofstream output(file, std::ios::binary);
    if (!output)
        return false;
    int ok = stbi_write_png_to_func(
        [](void* p, void* data, int size) {
            static_cast<std::ofstream*>(p)->write(static_cast<char*>(data), size);
        },
        &output, width, height, 4, pixels.data(), width * 4);
    return ok && output.good();
}
void flip(std::vector<uint8_t>& pixels, int width, int height) {
    size_t stride = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height / 2; ++y)
        std::swap_ranges(pixels.data() + y * stride, pixels.data() + (y + 1) * stride,
                         pixels.data() + (height - 1 - y) * stride);
}
const char* vertex = R"(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(location=3) in vec2 uv1;
layout(location=4) in vec4 vertexColor;
layout(location=5) in uvec4 joints;
layout(location=6) in vec4 weights;
uniform samplerBuffer palette;uniform int skinning,hasColor,topology;uniform float pointSize;
uniform mat4 model, vp;
uniform mat3 normalMatrix;
out vec3 N, P; out vec2 UV,UV1;out vec4 C;
mat4 bone(uint index){int i=int(index)*4;return mat4(texelFetch(palette,i),texelFetch(palette,i+1),texelFetch(palette,i+2),texelFetch(palette,i+3));}
void main(){vec4 p=model*vec4(position,1);N=normalMatrix*normal;
 if(skinning==1&&dot(weights,vec4(1))>.001){mat4 skin=bone(joints.x)*weights.x+bone(joints.y)*weights.y+bone(joints.z)*weights.z+bone(joints.w)*weights.w;
 p=skin*vec4(position,1);mat3 m=mat3(skin);N=abs(determinant(m))>1e-12?transpose(inverse(m))*normal:normal;}
 P=p.xyz;UV=uv;UV1=uv1;C=hasColor==1?vertexColor:vec4(1);gl_Position=vp*p;gl_PointSize=pointSize;}
)";
const char* fragment = R"(#version 330 core
in vec3 N,P;in vec2 UV,UV1;in vec4 C;out vec4 outColor;
uniform int topology;
uniform vec4 base;uniform int mode,unlit,materialUnlit;uniform vec3 eye,emission;
uniform sampler2D maps[6];uniform int enabled[6],uvSet[6],channel[6];uniform mat3 uvTransform[6];
uniform samplerCube radiance,irradiance;uniform sampler2D brdf;
uniform float metallic,roughness,normalScale,aoStrength,alphaCutoff,exposure,environment,rotation;
uniform int alphaMode,useNormals,useAO;
const float PI=3.14159265359;
vec2 coords(int i){vec2 q=(uvTransform[i]*vec3(uvSet[i]==1?UV1:UV,1)).xy;return vec2(q.x,1-q.y);}
// Sampler arrays require compile-time indices on OpenGL 3.3 / macOS.
vec4 sampleMap(int i,vec2 q){
 if(i==0)return texture(maps[0],q);if(i==1)return texture(maps[1],q);
 if(i==2)return texture(maps[2],q);if(i==3)return texture(maps[3],q);
 if(i==4)return texture(maps[4],q);return texture(maps[5],q);
}
vec3 rotate(vec3 d,float angle){float c=cos(angle),s=sin(angle);return vec3(c*d.x-s*d.z,d.y,s*d.x+c*d.z);}
vec3 studioDirection(vec3 d){return rotate(d,rotation);}
vec3 fresnel(float h,vec3 f0){return f0+(1-f0)*pow(1-h,5);}
float distribution(float nh,float r){float a=r*r,a2=a*a,d=nh*nh*(a2-1)+1;return a2/max(PI*d*d,1e-7);}
float geometry(float nv,float r){float a=r*r;return 2*nv/max(nv+sqrt(a*a+(1-a*a)*nv*nv),1e-6);}
vec3 light(vec3 n,vec3 v,vec3 l,vec3 energy,vec3 c,float m,float r,vec3 f0){
 vec3 h=normalize(v+l);float nl=max(dot(n,l),0),nv=max(dot(n,v),.0001),nh=max(dot(n,h),0);
 vec3 f=fresnel(max(dot(v,h),0),f0);
 vec3 spec=distribution(nh,r)*geometry(nv,r)*geometry(nl,r)*f/max(4*nv*nl,.0001);
 return ((1-f)*(1-m)*c/PI+spec)*energy*nl;
}
vec3 display(vec3 c){c=max(c*exp2(exposure),vec3(0));c=clamp((c*(2.51*c+.03))/(c*(2.43*c+.59)+.14),0,1);
 return mix(12.92*c,1.055*pow(c,vec3(1/2.4))-.055,step(vec3(.0031308),c));}
void main(){
 if(unlit==1){outColor=base;return;}
 if(topology==1&&length(gl_PointCoord*2-1)>1)discard;
 vec4 c=base*C;if(enabled[0]==1)c*=sampleMap(0,coords(0));
 if(mode==1)c=vec4(.45,.52,.57,1);
 if(alphaMode==1&&c.a<alphaCutoff)discard;
 if(alphaMode!=2||mode!=0)c.a=1;
 vec3 n=normalize(N);if(!gl_FrontFacing)n=-n;
 if(mode==0&&useNormals==1&&enabled[3]==1){
  vec2 q=coords(3);vec3 dp1=dFdx(P),dp2=dFdy(P);vec2 du1=dFdx(q),du2=dFdy(q);
  float det=du1.x*du2.y-du1.y*du2.x;
  vec3 t=(dp1*du2.y-dp2*du1.y),b=(-dp1*du2.x+dp2*du1.x);
  t=t-n*dot(n,t);b=b-n*dot(n,b);
  if(abs(det)>1e-10&&dot(t,t)>1e-15&&dot(b,b)>1e-15){
   vec3 mapped=sampleMap(3,q).xyz*2-1;mapped.xy*=normalScale;
   vec3 tangent=normalize(t)*sign(det);
   vec3 bitangent=normalize(cross(n,tangent))*sign(dot(cross(n,tangent),b)*det);
   n=normalize(mat3(tangent,bitangent,n)*mapped);
  }
 }
 if(mode==2){outColor=vec4(n*.5+.5,1);return;}
 if(materialUnlit==1&&mode==0){outColor=vec4(display(c.rgb),c.a);return;}
 float m=metallic,r=roughness;
 if(enabled[1]==1)m*=sampleMap(1,coords(1))[channel[1]];
 if(enabled[2]==1)r*=sampleMap(2,coords(2))[channel[2]];
 if(mode==1){m=0;r=.65;}
 m=clamp(m,0,1);r=clamp(r,.045,1);
 vec3 v=normalize(eye-P),f0=mix(vec3(.04),c.rgb,m);float nv=max(dot(n,v),.0001);
 vec3 color=light(n,v,rotate(normalize(vec3(.5,1,.7)),-rotation),vec3(2.3,2.15,1.95),c.rgb,m,r,f0);
 color+=light(n,v,rotate(normalize(vec3(-.8,.3,-.6)),-rotation),vec3(.55,.7,.95),c.rgb,m,r,f0);
 vec3 f=f0+(max(vec3(1-r),f0)-f0)*pow(1-nv,5);
 vec3 diffuse=texture(irradiance,studioDirection(n)).rgb*c.rgb*(1-f)*(1-m);
 vec2 lut=texture(brdf,vec2(nv,r)).rg;
 vec3 spec=textureLod(radiance,studioDirection(reflect(-v,n)),r*7).rgb*(f0*lut.x+lut.y);
 float ao=1;if(mode==0&&useAO==1&&enabled[4]==1)ao=mix(1,sampleMap(4,coords(4))[channel[4]],clamp(aoStrength,0,1));
 color+=(diffuse+spec)*environment*ao;
 vec3 e=emission;if(enabled[5]==1)e*=sampleMap(5,coords(5)).rgb;
 if(mode==0)color+=e;
 outColor=vec4(display(color),c.a);
})";
GLuint shader(GLenum type, const char* source) {
    GLuint id = glCreateShader(type);
    glShaderSource(id, 1, &source, nullptr);
    glCompileShader(id);
    GLint ok = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char error[4096]{};
        glGetShaderInfoLog(id, sizeof(error), nullptr, error);
        glDeleteShader(id);
        throw std::runtime_error(error);
    }
    return id;
}
} // namespace
Renderer::Renderer() {
    auto vs = shader(GL_VERTEX_SHADER, vertex), fs = shader(GL_FRAGMENT_SHADER, fragment);
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok)
        throw std::runtime_error("Echec du programme GPU");
    auto location = [&](const char* name) { return glGetUniformLocation(program_, name); };
    uniforms_.model = location("model");
    uniforms_.vp = location("vp");
    uniforms_.normal_matrix = location("normalMatrix");
    uniforms_.eye = location("eye");
    uniforms_.unlit = location("unlit");
    uniforms_.base = location("base");
    uniforms_.mode = location("mode");
    uniforms_.metallic = location("metallic");
    uniforms_.roughness = location("roughness");
    uniforms_.emission = location("emission");
    uniforms_.normal_scale = location("normalScale");
    uniforms_.ao_strength = location("aoStrength");
    uniforms_.alpha_mode = location("alphaMode");
    uniforms_.alpha_cutoff = location("alphaCutoff");
    uniforms_.exposure = location("exposure");
    uniforms_.environment = location("environment");
    uniforms_.rotation = location("rotation");
    uniforms_.use_normals = location("useNormals");
    uniforms_.use_ao = location("useAO");
    uniforms_.material_unlit = location("materialUnlit");
    uniforms_.skinning = location("skinning");
    uniforms_.palette = location("palette");
    uniforms_.has_color = location("hasColor");
    uniforms_.topology = location("topology");
    uniforms_.point_size = location("pointSize");
    glUseProgram(program_);
    for (unsigned i = 0; i < CY3D_MAP_COUNT; ++i) {
        auto indexed = [&](const char* name) {
            return location((std::string(name) + "[" + std::to_string(i) + "]").c_str());
        };
        uniforms_.maps[i] = indexed("maps");
        uniforms_.enabled[i] = indexed("enabled");
        uniforms_.uv_set[i] = indexed("uvSet");
        uniforms_.channel[i] = indexed("channel");
        uniforms_.transform[i] = indexed("uvTransform");
        glUniform1i(uniforms_.maps[i], static_cast<GLint>(i));
    }
    glUniform1i(location("radiance"), 6);
    glUniform1i(location("irradiance"), 7);
    glUniform1i(location("brdf"), 8);
    glUniform1i(uniforms_.palette, 9);
    glGenSamplers(static_cast<GLsizei>(samplers_.size()), samplers_.data());
    const GLint wraps[]{GL_REPEAT, GL_CLAMP_TO_EDGE, GL_MIRRORED_REPEAT};
    for (unsigned i = 0; i < samplers_.size(); ++i) {
        glSamplerParameteri(samplers_[i], GL_TEXTURE_WRAP_S, wraps[i / 3]);
        glSamplerParameteri(samplers_[i], GL_TEXTURE_WRAP_T, wraps[i % 3]);
        glSamplerParameteri(samplers_[i], GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glSamplerParameteri(samplers_[i], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    createEnvironment();
    createSplats();
    glGenVertexArrays(1, &skeleton_vao_);
    glGenBuffers(1, &skeleton_vbo_);
    glGenVertexArrays(1, &grid_);
    glGenBuffers(1, &gridVbo_);
    glGenFramebuffers(1, &framebuffer_);
    glGenTextures(1, &color_);
    glGenRenderbuffers(1, &depth_);
}
Renderer::~Renderer() {
    cancelUpload();
    active_.reset();
    cache_.clear();
    glDeleteProgram(program_);
    glDeleteVertexArrays(1, &grid_);
    glDeleteBuffers(1, &gridVbo_);
    glDeleteFramebuffers(1, &framebuffer_);
    glDeleteTextures(1, &color_);
    glDeleteTextures(1, &radiance_);
    glDeleteTextures(1, &irradiance_);
    glDeleteTextures(1, &brdf_);
    glDeleteProgram(splat_program_);
    glDeleteVertexArrays(1, &skeleton_vao_);
    glDeleteBuffers(1, &skeleton_vbo_);
    glDeleteSamplers(static_cast<GLsizei>(samplers_.size()), samplers_.data());
    glDeleteRenderbuffers(1, &depth_);
}
GLuint Renderer::draw(int width, int height) {
    width = std::clamp(width, 1, 8192);
    height = std::clamp(height, 1, 8192);
    if (width != width_ || height != height_) {
        width_ = width;
        height_ = height;
        glBindTexture(GL_TEXTURE_2D, color_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindRenderbuffer(GL_RENDERBUFFER, depth_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Cible GPU indisponible");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, width, height);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(.052f, .066f, .078f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(program_);
    updatePose();
    auto eye = camera.eye();
    auto vp = perspective(static_cast<float>(width) / height,
                          camera.nearPlane(),
                          camera.distance + camera.radius * 2000) *
              lookAt(eye, camera.target);
    glUniformMatrix4fv(uniforms_.vp, 1, GL_FALSE, vp.v);
    glUniform3f(uniforms_.eye, eye.x, eye.y, eye.z);
    glUniform1f(uniforms_.exposure, exposure);
    glUniform1f(uniforms_.environment, environment);
    glUniform1f(uniforms_.rotation, light_rotation);
    glUniform1i(uniforms_.use_normals, normal_maps);
    glUniform1i(uniforms_.use_ao, occlusion);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glUniform1f(uniforms_.point_size, point_size);
    glUniform1i(uniforms_.skinning, 0);
    glUniform1i(uniforms_.has_color, 0);
    glUniform1i(uniforms_.topology, 0);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_CUBE_MAP, radiance_);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_CUBE_MAP, irradiance_);
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, brdf_);
    for (unsigned unit = 6; unit < 9; ++unit)
        glBindSampler(unit, 0);
    auto identity = Mat4::identity();
    if (grid) {
        float unit = std::pow(10.f, std::floor(std::log10(camera.radius * .3f)));
        glBindVertexArray(grid_);
        if (unit != grid_unit_ || camera.ground != grid_ground_) {
            std::array<Cy3DVertex, 164> lines{};
            size_t index = 0;
            for (int i = -20; i <= 20; ++i) {
                float p = i * unit, extent = 20 * unit;
                lines[index++].position[0] = p;
                auto& a = lines[index - 1];
                a.position[1] = camera.ground;
                a.position[2] = -extent;
                lines[index++] = a;
                lines[index - 1].position[2] = extent;
                auto& b = lines[index++];
                b.position[0] = -extent;
                b.position[1] = camera.ground;
                b.position[2] = p;
                lines[index++] = b;
                lines[index - 1].position[0] = extent;
            }
            glBindBuffer(GL_ARRAY_BUFFER, gridVbo_);
            glBufferData(GL_ARRAY_BUFFER, sizeof(lines), lines.data(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Cy3DVertex), nullptr);
            grid_unit_ = unit;
            grid_ground_ = camera.ground;
        }
        glUniformMatrix4fv(uniforms_.model, 1, GL_FALSE, identity.v);
        glUniform1i(uniforms_.unlit, 1);
        glUniform4f(uniforms_.base, .16f, .23f, .26f, .5f);
        glDrawArrays(GL_LINES, 0, 164);
    }
    glUniform1i(uniforms_.unlit, 0);
    glUniform1i(uniforms_.mode, mode);
    glPolygonMode(GL_FRONT_AND_BACK, wire ? GL_LINE : GL_FILL);
    if (active_) {
        uint32_t lastMaterial = UINT32_MAX;
        auto drawInstance = [&](uint32_t index) {
            const auto& instance = active_->instances[index];
            const auto& mesh = active_->meshes[instance.data.mesh];
            const auto& material = active_->materials[mesh.material];
            if (mesh.topology == CY3D_SPLATS) {
                drawSplats(active_->meshes[instance.data.mesh], instance, vp, width, height);
                glUseProgram(program_);
                lastMaterial = UINT32_MAX;
                return;
            }
            glUniform1i(uniforms_.skinning, !mesh.bones.empty());
            glUniform1i(uniforms_.has_color, mesh.colors);
            glUniform1i(uniforms_.topology, static_cast<GLint>(mesh.topology));
            if (mesh.palette_texture) {
                glActiveTexture(GL_TEXTURE9);
                glBindTexture(GL_TEXTURE_BUFFER, mesh.palette_texture);
                glBindSampler(9, 0);
            }
            glUniformMatrix4fv(uniforms_.model, 1, GL_FALSE, instance.data.transform);
            glUniformMatrix3fv(uniforms_.normal_matrix, 1, GL_FALSE, instance.normals.data());
            if (mesh.material != lastMaterial) {
                lastMaterial = mesh.material;
                glUniform4fv(uniforms_.base, 1, material.color);
                glUniform1f(uniforms_.metallic, material.metallic);
                glUniform1f(uniforms_.roughness, material.roughness);
                glUniform3fv(uniforms_.emission, 1, material.emissive);
                glUniform1f(uniforms_.normal_scale, material.normal_scale);
                glUniform1f(uniforms_.ao_strength, material.occlusion_strength);
                glUniform1i(uniforms_.alpha_mode, static_cast<GLint>(material.alpha_mode));
                glUniform1f(uniforms_.alpha_cutoff, material.alpha_cutoff);
                glUniform1i(uniforms_.material_unlit, static_cast<GLint>(material.unlit));
                if (material.double_sided)
                    glDisable(GL_CULL_FACE);
                else {
                    glEnable(GL_CULL_FACE);
                    glCullFace(GL_BACK);
                }
                for (unsigned slot = 0; slot < CY3D_MAP_COUNT; ++slot) {
                    const auto& map = material.maps[slot];
                    size_t offset =
                        slot == CY3D_BASE_COLOR || slot == CY3D_EMISSIVE ? active_->textures.size() / 2 : 0;
                    GLuint texture = map.texture >= 0 ? active_->textures[offset + map.texture] : 0;
                    glActiveTexture(GL_TEXTURE0 + slot);
                    glBindTexture(GL_TEXTURE_2D, texture);
                    glBindSampler(slot, samplers_[map.wrap_u * 3 + map.wrap_v]);
                    glUniform1i(uniforms_.enabled[slot], texture && textured && mode == 0 ? 1 : 0);
                    glUniform1i(uniforms_.uv_set[slot], static_cast<GLint>(map.uv_set));
                    glUniform1i(uniforms_.channel[slot], static_cast<GLint>(map.channel));
                    glUniformMatrix3fv(uniforms_.transform[slot], 1, GL_FALSE, map.transform);
                }
            }
            glFrontFace(instance.winding);
            glBindVertexArray(mesh.vao);
            glDrawElements(mesh.topology == CY3D_POINTS ? GL_POINTS : GL_TRIANGLES, mesh.count,
                           GL_UNSIGNED_INT, nullptr);
        };
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        for (auto index : active_->opaque)
            drawInstance(index);
        if (mode == 0 || !active_->transparent.empty()) {
            auto forward = normalized(camera.target - eye);
            std::sort(active_->transparent.begin(), active_->transparent.end(), [&](uint32_t a, uint32_t b) {
                return dot(active_->instances[a].center - eye, forward) >
                       dot(active_->instances[b].center - eye, forward);
            });
            glEnable(GL_BLEND);
            glDepthMask(GL_FALSE);
        }
        for (auto index : active_->transparent)
            drawInstance(index);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        if (skeleton && active_->animation) {
            std::vector<Vec3> lines;
            for (size_t i = 0; i < active_->pose.size(); ++i) {
                const auto& node = active_->animation->nodes[i];
                if (node.bone && node.parent >= 0) {
                    const auto& a = active_->pose[i];
                    const auto& b = active_->pose[node.parent];
                    lines.push_back({a.v[12], a.v[13], a.v[14]});
                    lines.push_back({b.v[12], b.v[13], b.v[14]});
                }
            }
            glBindVertexArray(skeleton_vao_);
            glBindBuffer(GL_ARRAY_BUFFER, skeleton_vbo_);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lines.size() * sizeof(Vec3)), lines.data(),
                         GL_STREAM_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vec3), nullptr);
            glUniform1i(uniforms_.unlit, 1);
            glUniform1i(uniforms_.skinning, 0);
            glUniform1i(uniforms_.has_color, 0);
            glUniformMatrix4fv(uniforms_.model, 1, GL_FALSE, identity.v);
            glUniform4f(uniforms_.base, 1, .55f, .12f, 1);
            glDisable(GL_DEPTH_TEST);
            glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lines.size()));
            glEnable(GL_DEPTH_TEST);
        }
    }
    glFrontFace(GL_CCW);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glBindVertexArray(0);
    for (unsigned slot = 0; slot < CY3D_MAP_COUNT; ++slot)
        glBindSampler(slot, 0);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return color_;
}
bool Renderer::save(const fs::path& file) {
    if (width_ <= 0 || height_ <= 0)
        return false;
    std::vector<uint8_t> pixels(static_cast<size_t>(width_) * height_ * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    flip(pixels, width_, height_);
    return writePNG(file, width_, height_, pixels);
}
bool Renderer::saveWindow(const fs::path& file, int width, int height) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    flip(pixels, width, height);
    return writePNG(file, width, height, pixels);
}
} // namespace cy
