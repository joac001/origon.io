#include "render/SkinnedModel.h"

#include "core/FileSystem.h"
#include "core/Log.h"

#include <cgltf.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <map>

namespace origon {

bool SkinnedModel::load(const std::filesystem::path& path) {
    const std::string resolved = fs::resolve(path).string();
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, resolved.c_str(), &data) != cgltf_result_success ||
        cgltf_load_buffers(&options, data, resolved.c_str()) != cgltf_result_success) {
        log::error("SkinnedModel: cannot load {}", resolved);
        if (data) cgltf_free(data);
        return false;
    }
    if (data->skins_count == 0) {
        log::error("SkinnedModel: {} has no skin", resolved);
        cgltf_free(data);
        return false;
    }
    const cgltf_skin& skin = data->skins[0];
    std::map<const cgltf_node*, int> jointIndex;
    for (size_t i = 0; i < skin.joints_count; ++i) jointIndex[skin.joints[i]] = static_cast<int>(i);

    m_joints.resize(skin.joints_count);
    for (size_t i = 0; i < skin.joints_count; ++i) {
        const cgltf_node* node = skin.joints[i];
        Joint& joint = m_joints[i];
        joint.name = node->name ? node->name : "";
        if (node->has_translation) joint.translation = glm::make_vec3(node->translation);
        if (node->has_rotation) joint.rotation = glm::quat(node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2]);
        if (node->has_scale) joint.scale = glm::make_vec3(node->scale);
        auto it = node->parent ? jointIndex.find(node->parent) : jointIndex.end();
        if (it != jointIndex.end()) {
            joint.parent = it->second;
        } else if (node->parent) {
            float world[16];
            cgltf_node_transform_world(node->parent, world);
            joint.rootParent = glm::make_mat4(world);
        }
        if (skin.inverse_bind_matrices) {
            float m[16];
            cgltf_accessor_read_float(skin.inverse_bind_matrices, i, m, 16);
            joint.inverseBind = glm::make_mat4(m);
        }
    }
    // Topological order.
    std::vector<bool> placed(m_joints.size(), false);
    while (m_order.size() < m_joints.size()) {
        const size_t before = m_order.size();
        for (size_t i = 0; i < m_joints.size(); ++i) {
            const int parent = m_joints[i].parent;
            if (!placed[i] && (parent < 0 || placed[static_cast<size_t>(parent)])) {
                placed[i] = true;
                m_order.push_back(static_cast<int>(i));
            }
        }
        if (m_order.size() == before) break;
    }

    // Skinned mesh primitives.
    for (size_t n = 0; n < data->nodes_count; ++n) {
        const cgltf_node& node = data->nodes[n];
        if (!node.mesh || !node.skin) continue;
        for (size_t p = 0; p < node.mesh->primitives_count; ++p) {
            const cgltf_primitive& prim = node.mesh->primitives[p];
            const cgltf_accessor *positions = nullptr, *normals = nullptr, *joints = nullptr, *weights = nullptr, *uvs = nullptr;
            for (size_t a = 0; a < prim.attributes_count; ++a) {
                const cgltf_attribute& attr = prim.attributes[a];
                if (attr.type == cgltf_attribute_type_position) positions = attr.data;
                if (attr.type == cgltf_attribute_type_normal) normals = attr.data;
                if (attr.type == cgltf_attribute_type_joints && attr.index == 0) joints = attr.data;
                if (attr.type == cgltf_attribute_type_weights && attr.index == 0) weights = attr.data;
                if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) uvs = attr.data;
            }
            if (!positions || !joints || !weights) continue;
            std::vector<SkinnedVertex> vertices(positions->count);
            for (size_t i = 0; i < positions->count; ++i) {
                SkinnedVertex& v = vertices[i];
                float f[4] = {};
                cgltf_accessor_read_float(positions, i, f, 3);
                v.position = glm::vec3(f[0], f[1], f[2]);
                if (normals) {
                    cgltf_accessor_read_float(normals, i, f, 3);
                    v.normal = glm::vec3(f[0], f[1], f[2]);
                }
                if (uvs) {
                    cgltf_accessor_read_float(uvs, i, f, 2);
                    v.uv = glm::vec2(f[0], f[1]);
                } else {
                    v.uv = glm::vec2(0.0f);
                }
                cgltf_uint j[4] = {};
                cgltf_accessor_read_uint(joints, i, j, 4);
                for (int k = 0; k < 4; ++k) v.joints[k] = static_cast<uint16_t>(j[k]);
                cgltf_accessor_read_float(weights, i, f, 4);
                // Weights must sum to one (integer accessors may come back unnormalized).
                const float scale = weights->component_type == cgltf_component_type_r_32f ? 1.0f
                                    : weights->component_type == cgltf_component_type_r_16u ? 1.0f / 65535.0f
                                                                                             : 1.0f / 255.0f;
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    v.weights[k] = weights->normalized || weights->component_type == cgltf_component_type_r_32f ? f[k] : f[k] * scale;
                    sum += v.weights[k];
                }
                for (int k = 0; k < 4; ++k) v.weights[k] = sum > 1e-6f ? v.weights[k] / sum : (k == 0 ? 1.0f : 0.0f);
            }
            std::vector<uint32_t> indices(prim.indices ? prim.indices->count : vertices.size());
            for (size_t i = 0; i < indices.size(); ++i) {
                indices[i] = prim.indices ? static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, i)) : static_cast<uint32_t>(i);
            }
            Part part;
            part.mesh = Mesh(vertices, indices);
            if (prim.material) {
                if (prim.material->has_pbr_metallic_roughness) {
                    const float* c = prim.material->pbr_metallic_roughness.base_color_factor;
                    part.material.albedo = glm::vec3(c[0], c[1], c[2]);
                    part.material.roughness = prim.material->pbr_metallic_roughness.roughness_factor;
                    part.material.metallic = prim.material->pbr_metallic_roughness.metallic_factor;
                }
                part.tint = prim.material->name && std::string(prim.material->name) == "tint";
            }
            m_parts.push_back(std::move(part));
        }
    }

    // Animation clips.
    for (size_t a = 0; a < data->animations_count; ++a) {
        const cgltf_animation& anim = data->animations[a];
        Clip clip;
        clip.name = anim.name ? anim.name : "";
        for (size_t c = 0; c < anim.channels_count; ++c) {
            const cgltf_animation_channel& ch = anim.channels[c];
            auto it = jointIndex.find(ch.target_node);
            if (it == jointIndex.end()) continue;
            Channel channel;
            channel.joint = it->second;
            if (ch.target_path == cgltf_animation_path_type_translation) channel.path = 0;
            else if (ch.target_path == cgltf_animation_path_type_rotation) channel.path = 1;
            else if (ch.target_path == cgltf_animation_path_type_scale) channel.path = 2;
            else continue;
            const cgltf_accessor* input = ch.sampler->input;
            const cgltf_accessor* output = ch.sampler->output;
            channel.times.resize(input->count);
            channel.values.resize(output->count);
            const int components = channel.path == 1 ? 4 : 3;
            for (size_t i = 0; i < input->count; ++i) cgltf_accessor_read_float(input, i, &channel.times[i], 1);
            for (size_t i = 0; i < output->count; ++i) {
                float v[4] = {0, 0, 0, 0};
                cgltf_accessor_read_float(output, i, v, components);
                channel.values[i] = glm::vec4(v[0], v[1], v[2], v[3]);
            }
            const float first = channel.times.empty() ? 0.0f : channel.times.front();
            for (float& t : channel.times) t -= first; // Clips start at 0 (NLA strips may be offset).
            if (!channel.times.empty()) clip.duration = std::max(clip.duration, channel.times.back());
            clip.channels.push_back(std::move(channel));
        }
        m_clips.push_back(std::move(clip));
    }
    cgltf_free(data);
    log::info("SkinnedModel: {} joints, {} clips, {} parts", m_joints.size(), m_clips.size(), m_parts.size());
    return !m_parts.empty();
}

int SkinnedModel::findJoint(const std::string& name) const {
    for (size_t i = 0; i < m_joints.size(); ++i) {
        if (m_joints[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

int SkinnedModel::findClip(const std::string& name) const {
    for (size_t i = 0; i < m_clips.size(); ++i) {
        if (m_clips[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

float SkinnedModel::clipDuration(int clip) const {
    return clip >= 0 && clip < static_cast<int>(m_clips.size()) ? m_clips[static_cast<size_t>(clip)].duration : 0.0f;
}

SkinnedModel::Pose SkinnedModel::restPose() const {
    Pose pose;
    for (const Joint& joint : m_joints) {
        pose.translation.push_back(joint.translation);
        pose.rotation.push_back(joint.rotation);
        pose.scale.push_back(joint.scale);
    }
    return pose;
}

void SkinnedModel::sample(int clipIndex, float time, Pose& pose) const {
    if (clipIndex < 0 || clipIndex >= static_cast<int>(m_clips.size())) return;
    const Clip& clip = m_clips[static_cast<size_t>(clipIndex)];
    if (clip.duration > 0.0f) time = std::fmod(std::max(time, 0.0f), clip.duration);
    for (const Channel& channel : clip.channels) {
        if (channel.times.empty()) continue;
        size_t next = static_cast<size_t>(std::upper_bound(channel.times.begin(), channel.times.end(), time) - channel.times.begin());
        const size_t last = channel.times.size() - 1;
        const size_t i1 = std::min(next, last);
        const size_t i0 = next == 0 ? 0 : std::min(next - 1, last);
        const float span = channel.times[i1] - channel.times[i0];
        const float t = span > 1e-6f ? (time - channel.times[i0]) / span : 0.0f;
        const glm::vec4& a = channel.values[i0];
        const glm::vec4& b = channel.values[i1];
        const auto j = static_cast<size_t>(channel.joint);
        if (channel.path == 0) pose.translation[j] = glm::mix(glm::vec3(a), glm::vec3(b), t);
        else if (channel.path == 2) pose.scale[j] = glm::mix(glm::vec3(a), glm::vec3(b), t);
        else pose.rotation[j] = glm::slerp(glm::quat(a.w, a.x, a.y, a.z), glm::quat(b.w, b.x, b.y, b.z), t);
    }
}

void SkinnedModel::blend(Pose& a, const Pose& b, float weight) {
    weight = std::clamp(weight, 0.0f, 1.0f);
    for (size_t i = 0; i < a.rotation.size(); ++i) {
        a.translation[i] = glm::mix(a.translation[i], b.translation[i], weight);
        a.rotation[i] = glm::slerp(a.rotation[i], b.rotation[i], weight);
        a.scale[i] = glm::mix(a.scale[i], b.scale[i], weight);
    }
}

void SkinnedModel::rotateJoint(Pose& pose, int joint, const glm::vec3& modelAxis, float angle) const {
    if (joint < 0) return;
    std::vector<glm::mat4> globals, skin;
    evaluate(pose, globals, skin);
    const int parent = m_joints[static_cast<size_t>(joint)].parent;
    const glm::mat4 parentGlobal = parent >= 0 ? globals[static_cast<size_t>(parent)] : m_joints[static_cast<size_t>(joint)].rootParent;
    const glm::vec3 localAxis = glm::normalize(glm::inverse(glm::mat3(parentGlobal)) * modelAxis);
    auto& r = pose.rotation[static_cast<size_t>(joint)];
    r = glm::angleAxis(angle, localAxis) * r;
}

void SkinnedModel::evaluate(const Pose& pose, std::vector<glm::mat4>& globals, std::vector<glm::mat4>& skin) const {
    globals.assign(m_joints.size(), glm::mat4(1.0f));
    skin.assign(m_joints.size(), glm::mat4(1.0f));
    for (int index : m_order) {
        const auto i = static_cast<size_t>(index);
        const Joint& joint = m_joints[i];
        const glm::mat4 local = glm::translate(glm::mat4(1.0f), pose.translation[i]) * glm::mat4_cast(pose.rotation[i]) *
                                glm::scale(glm::mat4(1.0f), pose.scale[i]);
        globals[i] = (joint.parent >= 0 ? globals[static_cast<size_t>(joint.parent)] : joint.rootParent) * local;
        skin[i] = globals[i] * joint.inverseBind;
    }
}

} // namespace origon
