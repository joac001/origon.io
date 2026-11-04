#pragma once

#include "render/Mesh.h"
#include "render/Renderer.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace origon {

// Skinned glTF model with skeletal animation clips (from tools/blender/gen_character.py).
class SkinnedModel {
public:
    struct Pose {
        std::vector<glm::vec3> translation;
        std::vector<glm::quat> rotation;
        std::vector<glm::vec3> scale;
    };
    struct Part {
        Mesh mesh;
        Material material;
        bool tint = false; // Material named "tint": recolored per player.
        std::string name;         // glTF node name (character piece, e.g. "m_top_jacket").
        std::string materialName; // glTF material name (e.g. "skin", "hair", "top").
    };

    // With a texture cache, materials named "char_<id>" / "charalpha_<id>" (alpha-tested) load
    // assets/textures/characters/<id>.{jpg,png} and an optional <id>_normal map.
    bool load(const std::filesystem::path& path, class TextureCache* textures = nullptr);

    int jointCount() const { return static_cast<int>(m_joints.size()); }
    int findJoint(const std::string& name) const;
    int findClip(const std::string& name) const;
    float clipDuration(int clip) const;
    const std::vector<Part>& parts() const { return m_parts; }

    Pose restPose() const;
    // Overwrites the joints animated by the clip (time wraps around the clip duration).
    void sample(int clip, float time, Pose& pose) const;
    static void blend(Pose& a, const Pose& b, float weight);
    // Per-joint weights (see jointMask): blends only the masked part of the skeleton.
    static void blend(Pose& a, const Pose& b, float weight, const std::vector<float>& mask);
    // 1 for the joint and all its descendants, 0 elsewhere.
    std::vector<float> jointMask(int root) const;
    // Rotates a joint so the direction from it to `child` (a descendant joint) points along
    // `target` (model space). Used for two-bone IK.
    void aimJoint(Pose& pose, int joint, int child, const glm::vec3& target) const;
    // Adds a rotation (axis in model space) to one joint's local rotation, given the pose's globals.
    void rotateJoint(Pose& pose, int joint, const glm::vec3& modelAxis, float angle) const;
    // Model-space joint matrices (globals) and skinning matrices (global * inverse bind).
    void evaluate(const Pose& pose, std::vector<glm::mat4>& globals, std::vector<glm::mat4>& skin) const;

private:
    struct Joint {
        std::string name;
        int parent = -1;
        glm::mat4 rootParent{1.0f}; // Transform of non-joint ancestors (root joints only).
        glm::vec3 translation{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
        glm::mat4 inverseBind{1.0f};
    };
    struct Channel {
        int joint = 0;
        int path = 0; // 0 translation, 1 rotation, 2 scale
        std::vector<float> times;
        std::vector<glm::vec4> values;
    };
    struct Clip {
        std::string name;
        float duration = 0.0f;
        std::vector<Channel> channels;
    };

    std::vector<Joint> m_joints;
    std::vector<int> m_order; // Parents before children.
    std::vector<Clip> m_clips;
    std::vector<Part> m_parts;
};

} // namespace origon
