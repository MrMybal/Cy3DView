// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "cy3d_plugin.h"
#include "cy3d_mapped_file.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
namespace {
namespace fs = std::filesystem;
struct Model {
    Cy3DScene scene{};
    Cy3DMesh mesh{};
    Cy3DInstance instance{};
    Cy3DMaterial material = Cy3D_DefaultMaterial();
    std::vector<Cy3DVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<float> colors;
    std::vector<Cy3DSplat> splats;
    const char* warning =
        "Gaussian Splatting : couleur SH de degre 0 ; coefficients directionnels superieurs non affiches.";
    Model() {
        instance.node = -1;
        for (int i = 0; i < 4; ++i)
            instance.transform[i * 5] = 1;
        for (int i = 0; i < 3; ++i) {
            scene.bounds_min[i] = std::numeric_limits<float>::max();
            scene.bounds_max[i] = -scene.bounds_min[i];
        }
        material.name = "Nuage";
        std::fill_n(material.color, 4, 1.f);
        material.unlit = 1;
    }
};
bool cancelled(const Cy3DHost* h) {
    return h->is_cancelled && h->is_cancelled(h->context);
}
void budget(uint64_t count, bool splats, const Cy3DHost* h) {
    if (count > 0x7fffffff ||
        count > h->max_output_bytes / (sizeof(Cy3DVertex) + 20 + (splats ? sizeof(Cy3DSplat) : 0)))
        throw std::runtime_error("Nuage trop volumineux");
    if (cancelled(h))
        throw std::runtime_error("Chargement annule");
}
float linear(float v) {
    v = std::clamp(v, 0.f, 1.f);
    return v <= .04045f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f);
}
void append(Model& m, const std::vector<double>& values,
            const std::unordered_map<std::string, size_t>& properties, bool gaussian, const Cy3DHost* h,
            bool normalized = false, bool normalizedAlpha = false) {
    if ((m.vertices.size() & 4095) == 0)
        budget(m.vertices.size() + 1, gaussian, h);
    auto get = [&](const char* name, double fallback = 0) {
        auto i = properties.find(name);
        return i == properties.end() ? fallback : values[i->second];
    };
    Cy3DVertex v{};
    v.normal[1] = 1;
    const char* axes[]{"x", "y", "z"};
    Cy3DSplat splat{};
    splat.rotation[3] = 1;
    for (int i = 0; i < 3; ++i) {
        v.position[i] = static_cast<float>(get(axes[i]));
        if (!std::isfinite(v.position[i]))
            throw std::runtime_error("Position de point non finie");
    }
    float color[4]{.72f, .8f, .82f, 1};
    if (gaussian) {
        for (int i = 0; i < 3; ++i) {
            splat.scale[i] = std::exp(static_cast<float>(get(("scale_" + std::to_string(i)).c_str())));
            color[i] = linear(
                static_cast<float>(.5 + .28209479177387814 * get(("f_dc_" + std::to_string(i)).c_str())));
            if (!std::isfinite(splat.scale[i]) || splat.scale[i] <= 0)
                throw std::runtime_error("Echelle de splat invalide");
        }
        for (int i = 0; i < 4; ++i)
            splat.rotation[i] = static_cast<float>(get(("rot_" + std::to_string((i + 1) % 4)).c_str()));
        float length = 0;
        for (float q : splat.rotation)
            length += q * q;
        if (!std::isfinite(length) || length < 1e-20f)
            throw std::runtime_error("Rotation de splat invalide");
        for (float& q : splat.rotation)
            q /= std::sqrt(length);
        float opacity = static_cast<float>(get("opacity"));
        if (!std::isfinite(opacity))
            throw std::runtime_error("Opacite de splat invalide");
        color[3] = 1 / (1 + std::exp(-opacity));
        m.splats.push_back(splat);
    } else if (properties.contains("red")) {
        double divisor = normalized ? 1 : 255;
        const char* names[]{"red", "green", "blue"};
        for (int i = 0; i < 3; ++i)
            color[i] = linear(static_cast<float>(get(names[i], divisor) / divisor));
        double alphaDivisor = normalizedAlpha ? 1 : 255;
        color[3] = std::clamp(static_cast<float>(get("alpha", alphaDivisor) / alphaDivisor), 0.f, 1.f);
    }
    for (float value : color)
        if (!std::isfinite(value))
            throw std::runtime_error("Couleur de point non finie");
    if (color[3] < 1)
        m.material.alpha_mode = CY3D_BLEND;
    for (int i = 0; i < 3; ++i) {
        float radius = gaussian ? 3 * std::max({splat.scale[0], splat.scale[1], splat.scale[2]}) : 0;
        m.scene.bounds_min[i] = std::min(m.scene.bounds_min[i], v.position[i] - radius);
        m.scene.bounds_max[i] = std::max(m.scene.bounds_max[i], v.position[i] + radius);
    }
    m.vertices.push_back(v);
    m.colors.insert(m.colors.end(), color, color + 4);
}
struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool binary = false, big = false;
    std::string_view line() {
        auto begin = p;
        while (p < end && *p != '\n')
            ++p;
        auto stop = p;
        if (p < end)
            ++p;
        return {reinterpret_cast<const char*>(begin), static_cast<size_t>(stop - begin)};
    }
    double number(const std::string& type) {
        if (type != "char" && type != "uchar" && type != "int8" && type != "uint8" && type != "short" &&
            type != "ushort" && type != "int16" && type != "uint16" && type != "int" && type != "uint" &&
            type != "int32" && type != "uint32" && type != "float" && type != "float32" && type != "double" &&
            type != "float64")
            throw std::runtime_error("Type de propriete PLY inconnu");
        if (!binary) {
            while (p < end && std::isspace(*p))
                ++p;
            if (p < end && *p == '+')
                ++p;
            auto begin = reinterpret_cast<const char*>(p);
            double value;
            auto r = std::from_chars(begin, reinterpret_cast<const char*>(end), value);
            if (r.ec != std::errc() || !std::isfinite(value))
                throw std::runtime_error("Nombre de point invalide");
            p = reinterpret_cast<const uint8_t*>(r.ptr);
            return value;
        }
        unsigned size = type == "char" || type == "uchar" || type == "int8" || type == "uint8"       ? 1
                        : type == "short" || type == "ushort" || type == "int16" || type == "uint16" ? 2
                        : type == "double" || type == "float64"                                      ? 8
                                                                                                     : 4;
        if (static_cast<size_t>(end - p) < size)
            throw std::runtime_error("PLY tronque");
        uint64_t bits = 0;
        for (unsigned i = 0; i < size; ++i)
            bits |= uint64_t(p[big ? size - i - 1 : i]) << (i * 8);
        p += size;
        if (type == "float" || type == "float32")
            return std::bit_cast<float>(static_cast<uint32_t>(bits));
        if (type == "double" || type == "float64")
            return std::bit_cast<double>(bits);
        if (type == "char" || type == "int8")
            return static_cast<int8_t>(bits);
        if (type == "short" || type == "int16")
            return static_cast<int16_t>(bits);
        if (type == "int" || type == "int32")
            return static_cast<int32_t>(bits);
        if (type == "uchar" || type == "uint8" || type == "ushort" || type == "uint16" || type == "uint" ||
            type == "uint32")
            return static_cast<double>(bits);
        throw std::runtime_error("Type de propriete PLY inconnu");
    }
};
bool ply(Model& m, Reader& r, const Cy3DHost* host) {
    auto magic = r.line();
    if (!magic.empty() && magic.back() == '\r')
        magic.remove_suffix(1);
    if (magic != "ply")
        throw std::runtime_error("En-tete PLY invalide");
    struct Property {
        std::string name, type, count_type;
    };
    std::vector<Property> fields;
    std::unordered_map<std::string, size_t> map;
    uint64_t count = 0;
    bool vertex = false, finished = false, format = false;
    const uint8_t* start = r.p;
    while (r.p < r.end) {
        if (r.p - start > 1024 * 1024)
            throw std::runtime_error("En-tete PLY trop long");
        std::istringstream line{std::string(r.line())};
        std::string tag;
        line >> tag;
        if (tag == "format") {
            std::string mode, version;
            line >> mode >> version;
            if (version != "1.0")
                throw std::runtime_error("Version PLY inconnue");
            r.binary = mode != "ascii";
            r.big = mode == "binary_big_endian";
            if (mode != "ascii" && mode != "binary_little_endian" && mode != "binary_big_endian")
                throw std::runtime_error("Encodage PLY inconnu");
            format = true;
        }
        if (tag == "element") {
            std::string name;
            uint64_t size = 0;
            line >> name >> size;
            if (!line)
                throw std::runtime_error("Element PLY invalide");
            vertex = name == "vertex";
            if (vertex) {
                if (count)
                    throw std::runtime_error("Elements vertex multiples");
                count = size;
            } else if (size) {
                if (name == "face")
                    return false;
                if (!count)
                    throw std::runtime_error("Element avant les sommets non pris en charge");
            }
        }
        if (tag == "property" && vertex) {
            Property f;
            line >> f.type;
            if (f.type == "list") {
                line >> f.count_type >> f.type;
            }
            line >> f.name;
            if (!line)
                throw std::runtime_error("Propriete PLY invalide");
            if (map.contains(f.name))
                throw std::runtime_error("Propriete PLY dupliquee");
            map[f.name] = fields.size();
            fields.push_back(f);
        }
        if (tag == "end_header") {
            finished = true;
            break;
        }
    }
    if (!finished || !format || !count || !map.contains("x") || !map.contains("y") || !map.contains("z"))
        throw std::runtime_error("PLY sans positions valides");
    bool gaussian = map.contains("f_dc_0") || map.contains("scale_0");
    bool normalized =
        map.contains("red") && (fields[map["red"]].type == "float" || fields[map["red"]].type == "float32" ||
                                fields[map["red"]].type == "double" || fields[map["red"]].type == "float64");
    if (gaussian)
        for (const char* name : {"f_dc_0", "f_dc_1", "f_dc_2", "opacity", "scale_0", "scale_1", "scale_2",
                                 "rot_0", "rot_1", "rot_2", "rot_3"})
            if (!map.contains(name))
                throw std::runtime_error("Proprietes Gaussian Splatting incompletes");
    budget(count, gaussian, host);
    m.vertices.reserve(static_cast<size_t>(count));
    m.colors.reserve(static_cast<size_t>(count) * 4);
    if (gaussian)
        m.splats.reserve(static_cast<size_t>(count));
    std::vector<double> values(fields.size());
    for (uint64_t i = 0; i < count; ++i) {
        for (size_t j = 0; j < fields.size(); ++j) {
            const auto& f = fields[j];
            if (!f.count_type.empty()) {
                double n = r.number(f.count_type);
                if (!std::isfinite(n) || n < 0 || n > 1000000 || std::floor(n) != n)
                    throw std::runtime_error("Liste PLY invalide");
                for (uint64_t k = 0; k < static_cast<uint64_t>(n); ++k)
                    r.number(f.type);
                values[j] = 0;
            } else
                values[j] = r.number(f.type);
        }
        bool normalizedAlpha =
            map.contains("alpha") &&
            (fields[map["alpha"]].type == "float" || fields[map["alpha"]].type == "float32" ||
             fields[map["alpha"]].type == "double" || fields[map["alpha"]].type == "float64");
        append(m, values, map, gaussian, host, normalized, normalizedAlpha);
        if ((i & 4095) == 0 && host->progress)
            host->progress(host->context, static_cast<float>(i) / count);
    }
    m.mesh.topology = gaussian ? CY3D_SPLATS : CY3D_POINTS;
    if (gaussian) {
        m.scene.warnings = &m.warning;
        m.scene.warning_count = 1;
    }
    return true;
}
void packed(Model& m, const cy::MappedFile& file, const Cy3DHost* host) {
    if (file.size() % 32)
        throw std::runtime_error("Fichier .splat tronque");
    uint64_t count = file.size() / 32;
    budget(count, true, host);
    m.vertices.reserve(static_cast<size_t>(count));
    m.colors.reserve(static_cast<size_t>(count) * 4);
    m.splats.reserve(static_cast<size_t>(count));
    std::unordered_map<std::string, size_t> props{
        {"x", 0},       {"y", 1},      {"z", 2},      {"scale_0", 3}, {"scale_1", 4},
        {"scale_2", 5}, {"f_dc_0", 6}, {"f_dc_1", 7}, {"f_dc_2", 8},  {"opacity", 9},
        {"rot_0", 10},  {"rot_1", 11}, {"rot_2", 12}, {"rot_3", 13}};
    std::vector<double> values(14);
    for (uint64_t i = 0; i < count; ++i) {
        auto p = file.data() + i * 32;
        Reader reader{p, p + 24, true, false};
        for (int j = 0; j < 6; ++j)
            values[j] = reader.number("float");
        for (int j = 3; j < 6; ++j) {
            if (values[j] <= 0)
                throw std::runtime_error("Echelle .splat invalide");
            values[j] = std::log(values[j]);
        }
        for (int j = 0; j < 3; ++j)
            values[6 + j] = (p[24 + j] / 255. - .5) / .28209479177387814;
        double opacity = std::clamp(p[27] / 255., 1e-8, 1 - 1e-8);
        values[9] = std::log(opacity / (1 - opacity));
        for (int j = 0; j < 4; ++j)
            values[10 + j] = (int(p[28 + j]) - 128) / 128.;
        append(m, values, props, true, host);
    }
    m.mesh.topology = CY3D_SPLATS;
}
void text(Model& m, Reader& r, bool pts, const Cy3DHost* host) {
    uint64_t declared = 0;
    bool first = true;
    while (r.p < r.end) {
        auto line = r.line();
        if (line.empty() || line.front() == '#')
            continue;
        Reader numbers{reinterpret_cast<const uint8_t*>(line.data()),
                       reinterpret_cast<const uint8_t*>(line.data() + line.size())};
        std::vector<double> values;
        while (numbers.p < numbers.end) {
            while (numbers.p < numbers.end && std::isspace(*numbers.p))
                ++numbers.p;
            if (numbers.p == numbers.end || *numbers.p == '#')
                break;
            values.push_back(numbers.number("double"));
            if (values.size() > 8)
                throw std::runtime_error("Trop de colonnes dans le nuage");
        }
        if (values.empty())
            continue;
        if (first && pts) {
            if (values.size() != 1 || !std::isfinite(values[0]) || values[0] < 1 || values[0] > 0x7fffffff ||
                std::floor(values[0]) != values[0])
                throw std::runtime_error("Nombre de points PTS invalide");
            declared = static_cast<uint64_t>(values[0]);
            budget(declared, false, host);
            first = false;
            continue;
        }
        first = false;
        if (values.size() != 3 && values.size() != 6 && values.size() != 7)
            throw std::runtime_error("Nuage : XYZ, XYZ RGB ou XYZ intensite RGB attendu");
        std::unordered_map<std::string, size_t> props{{"x", 0}, {"y", 1}, {"z", 2}};
        if (values.size() > 3) {
            size_t c = values.size() == 7 ? 4 : 3;
            props["red"] = c;
            props["green"] = c + 1;
            props["blue"] = c + 2;
        }
        append(m, values, props, false, host);
    }
    if (pts && m.vertices.size() != declared)
        throw std::runtime_error("Nombre de points PTS incorrect");
    m.mesh.topology = CY3D_POINTS;
}
int load(const char* filename, const Cy3DHost* host, Cy3DScene** output, char* error, size_t size) {
    if (output)
        *output = nullptr;
    try {
        if (!filename || !host || !output || host->api_version != CY3D_API_VERSION)
            throw std::runtime_error("ABI invalide");
        fs::path path(std::u8string_view(reinterpret_cast<const char8_t*>(filename)));
        cy::MappedFile file(path);
        auto model = std::make_unique<Model>();
        auto& m = *model;
        Reader r{file.data(), file.data() + file.size()};
        auto ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".ply") {
            if (!ply(m, r, host))
                return -3;
        } else if (ext == ".splat")
            packed(m, file, host);
        else
            text(m, r, ext == ".pts", host);
        if (m.vertices.empty())
            throw std::runtime_error("Nuage vide");
        budget(m.vertices.size(), !m.splats.empty(), host);
        m.indices.resize(m.vertices.size());
        std::iota(m.indices.begin(), m.indices.end(), 0u);
        m.mesh.name = "Nuage";
        m.mesh.vertices = m.vertices.data();
        m.mesh.indices = m.indices.data();
        m.mesh.vertex_count = m.mesh.index_count = static_cast<uint32_t>(m.vertices.size());
        m.mesh.colors = m.colors.data();
        m.mesh.splats = m.splats.empty() ? nullptr : m.splats.data();
        if (!m.splats.empty()) {
            m.material.alpha_mode = CY3D_BLEND;
            m.material.name = "Gaussian Splats";
        }
        for (int i = 0; i < 3; ++i)
            m.mesh.center[i] = (m.scene.bounds_min[i] + m.scene.bounds_max[i]) * .5f;
        m.scene.struct_size = sizeof(Cy3DScene);
        m.scene.meshes = &m.mesh;
        m.scene.mesh_count = 1;
        m.scene.instances = &m.instance;
        m.scene.instance_count = 1;
        m.scene.materials = &m.material;
        m.scene.material_count = 1;
        m.scene.owner = &m;
        m.scene.memory_bytes =
            m.vertices.size() * (sizeof(Cy3DVertex) + 20) + m.splats.size() * sizeof(Cy3DSplat);
        *output = &model.release()->scene;
        return 0;
    } catch (const std::exception& e) {
        if (error && size)
            std::snprintf(error, size, "%s", e.what());
    } catch (...) {
        if (error && size)
            std::snprintf(error, size, "Erreur de nuage");
    }
    return host && cancelled(host) ? -2 : -1;
}
void release(Cy3DScene* s) {
    delete static_cast<Model*>(s->owner);
}
} // namespace
extern "C" CY3D_EXPORT const Cy3DPlugin* Cy3D_GetPlugin() {
    static const Cy3DPlugin plugin{
        CY3D_API_VERSION, sizeof(Cy3DPlugin), "Nuages et Gaussian Splats", "1.0", "ply;xyz;pts;splat", load,
        release};
    return &plugin;
}
