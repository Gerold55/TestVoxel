#include "Model.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <glm/gtc/type_ptr.hpp>

using json = nlohmann::json;

// --- Mesh Implementation ---
Mesh::Mesh(const std::vector<ModelVertex>& vertices) {
    vertexCount = static_cast<int>(vertices.size());
    if (vertexCount == 0) return;
    
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(ModelVertex), vertices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)offsetof(ModelVertex, position));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)offsetof(ModelVertex, normal));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)offsetof(ModelVertex, texCoords));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

void Mesh::draw() const {
    if (VAO == 0 || vertexCount == 0) return;
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    glBindVertexArray(0);
}

// --- Helper Binary Readers for .b3d ---
std::string readB3DString(std::ifstream& file) {
    std::string str = "";
    char c;
    while (file.get(c) && c != '\0') {
        str += c;
    }
    return str;
}

int readB3DInt(std::ifstream& file) {
    int val = 0;
    file.read(reinterpret_cast<char*>(&val), sizeof(int));
    return val;
}

float readB3DFloat(std::ifstream& file) {
    float val = 0.0f;
    file.read(reinterpret_cast<char*>(&val), sizeof(float));
    return val;
}

// --- AnimatedModel Implementation ---
void AnimatedModel::loadFromFile(const std::string& filepath) {
    if (filepath.size() >= 4 && (filepath.substr(filepath.size() - 4) == ".b3d" || filepath.substr(filepath.size() - 4) == ".B3D")) {
        loadFromB3D(filepath);
    } else {
        loadFromJson(filepath);
    }
}

void AnimatedModel::loadFromJson(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[Model Error] Failed to open JSON model file: " << filepath << std::endl;
        return;
    }

    json j;
    file >> j;

    if (!j.contains("minecraft:geometry")) {
        std::cerr << "[Model Error] Invalid JSON format: missing 'minecraft:geometry' in " << filepath << std::endl;
        return;
    }

    auto geom = j["minecraft:geometry"][0];

    for (auto& bData : geom["bones"]) {
        std::string name = bData["name"];
        auto bone = std::make_shared<Bone>();
        bone->name = name;

        if (bData.contains("pivot")) {
            bone->pivot = glm::vec3(bData["pivot"][0], bData["pivot"][1], bData["pivot"][2]) / 16.0f;
        }

        if (bData.contains("cubes")) {
            auto cubeData = bData["cubes"][0];
            auto origin = cubeData["origin"];
            auto size = cubeData["size"];
            
            float ox = origin[0], oy = origin[1], oz = origin[2];
            float sx = size[0], sy = size[1], sz = size[2];
            float x0 = ox / 16.0f, y0 = oy / 16.0f, z0 = oz / 16.0f;
            float x1 = (ox + sx) / 16.0f, y1 = (oy + sy) / 16.0f, z1 = (oz + sz) / 16.0f;

            std::vector<ModelVertex> vertices = {
                { {x0, y0, z1}, {0,0,1}, {0,0} }, { {x1, y0, z1}, {0,0,1}, {1,0} }, { {x1, y1, z1}, {0,0,1}, {1,1} },
                { {x0, y0, z1}, {0,0,1}, {0,0} }, { {x1, y1, z1}, {0,0,1}, {1,1} }, { {x0, y1, z1}, {0,0,1}, {0,1} },
                { {x1, y0, z0}, {0,0,-1}, {0,0} }, { {x0, y0, z0}, {0,0,-1}, {1,0} }, { {x0, y1, z0}, {0,0,-1}, {1,1} },
                { {x1, y0, z0}, {0,0,-1}, {0,0} }, { {x0, y1, z0}, {0,0,-1}, {1,1} }, { {x1, y1, z0}, {0,0,-1}, {0,1} },
                { {x0, y0, z0}, {-1,0,0}, {0,0} }, { {x0, y0, z1}, {-1,0,0}, {1,0} }, { {x0, y1, z1}, {-1,0,0}, {1,1} },
                { {x0, y0, z0}, {-1,0,0}, {0,0} }, { {x0, y1, z1}, {-1,0,0}, {1,1} }, { {x0, y1, z0}, {-1,0,0}, {0,1} },
                { {x1, y0, z1}, {1,0,0}, {0,0} }, { {x1, y0, z0}, {1,0,0}, {1,0} }, { {x1, y1, z0}, {1,0,0}, {1,1} },
                { {x1, y0, z1}, {1,0,0}, {0,0} }, { {x1, y1, z0}, {1,0,0}, {1,1} }, { {x1, y1, z1}, {1,0,0}, {0,1} },
                { {x0, y1, z1}, {0,1,0}, {0,0} }, { {x1, y1, z1}, {0,1,0}, {1,0} }, { {x1, y1, z0}, {0,1,0}, {1,1} },
                { {x0, y1, z1}, {0,1,0}, {0,0} }, { {x1, y1, z0}, {0,1,0}, {1,1} }, { {x0, y1, z0}, {0,1,0}, {0,1} },
                { {x0, y0, z0}, {0,-1,0}, {0,0} }, { {x1, y0, z0}, {0,-1,0}, {1,0} }, { {x1, y0, z1}, {0,-1,0}, {1,1} },
                { {x0, y0, z0}, {0,-1,0}, {0,0} }, { {x1, y0, z1}, {0,-1,0}, {1,1} }, { {x1, y0, z1}, {0,-1,0}, {0,1} }
            };
            bone->mesh = std::make_shared<Mesh>(vertices);
        }

        boneMap[name] = bone;

        if (bData.contains("parent")) {
            std::string parentName = bData["parent"];
            if (boneMap.find(parentName) != boneMap.end()) {
                boneMap[parentName]->children.push_back(bone);
            }
        } else {
            rootBones.push_back(bone);
        }
    }
}

void parseB3DNodeRecursive(std::ifstream& file, std::streamoff chunkEnd, std::shared_ptr<Bone> parentBone, AnimatedModel* model) {
    while (static_cast<std::streamoff>(file.tellg()) < chunkEnd && file.good()) {
        char tag[4];
        file.read(tag, 4);
        int chunkSize = readB3DInt(file);
        std::streamoff nextChunkPos = static_cast<std::streamoff>(file.tellg()) + static_cast<std::streamoff>(chunkSize) - 8;

        std::string tagName(tag, 4);

        if (tagName == "NODE") {
            std::string nodeName = readB3DString(file);
            float px = readB3DFloat(file);
            float py = readB3DFloat(file);
            float pz = readB3DFloat(file);
            float sx = readB3DFloat(file);
            float sy = readB3DFloat(file);
            float sz = readB3DFloat(file);
            float rx = readB3DFloat(file);
            float ry = readB3DFloat(file);
            float rz = readB3DFloat(file);
            float rw = readB3DFloat(file);

            auto bone = std::make_shared<Bone>();
            bone->name = nodeName.empty() ? "bone_" + std::to_string(model->boneMap.size()) : nodeName;
            bone->position = glm::vec3(px, py, pz);
            bone->pivot = glm::vec3(0.0f);

            model->boneMap[bone->name] = bone;
            if (parentBone) {
                parentBone->children.push_back(bone);
            } else {
                model->rootBones.push_back(bone);
            }

            parseB3DNodeRecursive(file, nextChunkPos, bone, model);
        }
        else if (tagName == "MESH") {
            int brushId = readB3DInt(file);
            std::vector<ModelVertex> meshVertices;

            std::streamoff meshEnd = nextChunkPos;
            while (static_cast<std::streamoff>(file.tellg()) < meshEnd && file.good()) {
                char mTag[4];
                file.read(mTag, 4);
                int mSize = readB3DInt(file);
                std::streamoff nextMPos = static_cast<std::streamoff>(file.tellg()) + static_cast<std::streamoff>(mSize) - 8;
                std::string mTagName(mTag, 4);

                if (mTagName == "VRTS") {
                    int flags = readB3DInt(file);
                    int texCoordSets = readB3DInt(file);
                    int texCoordSetSize = readB3DInt(file);
                    int vCount = (mSize - 20) / (12 + 12 + (texCoordSets * texCoordSetSize * 4));

                    for (int i = 0; i < vCount; ++i) {
                        ModelVertex v;
                        v.position.x = readB3DFloat(file);
                        v.position.y = readB3DFloat(file);
                        v.position.z = readB3DFloat(file);
                        v.normal.x = readB3DFloat(file);
                        v.normal.y = readB3DFloat(file);
                        v.normal.z = readB3DFloat(file);
                        file.seekg(4, std::ios::cur);
                        if (texCoordSets > 0) {
                            v.texCoords.x = readB3DFloat(file);
                            v.texCoords.y = readB3DFloat(file);
                            if (texCoordSets > 1) file.seekg(8, std::ios::cur);
                        }
                        meshVertices.push_back(v);
                    }
                }
                file.seekg(nextMPos, std::ios::beg);
            }

            if (parentBone && !meshVertices.empty()) {
                parentBone->mesh = std::make_shared<Mesh>(meshVertices);
            }
        }

        file.seekg(nextChunkPos, std::ios::beg);
    }
}

void AnimatedModel::loadFromB3D(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[Model Error] Failed to open .b3d binary file: " << filepath << std::endl;
        return;
    }

    char tag[4];
    file.read(tag, 4);
    if (std::string(tag, 4) != "BB3D") {
        std::cerr << "[Model Error] Invalid .b3d file header: " << filepath << std::endl;
        return;
    }

    int fileSize = readB3DInt(file);
    int version = readB3DInt(file);

    parseB3DNodeRecursive(file, static_cast<std::streamoff>(fileSize) + 8, nullptr, this);
    std::cout << "[Model] Successfully loaded .b3d model: " << filepath << " with " << boneMap.size() << " bones." << std::endl;
}

std::shared_ptr<Bone> AnimatedModel::getBone(const std::string& name) {
    auto it = boneMap.find(name);
    if (it != boneMap.end()) {
        return it->second;
    }
    return nullptr;
}

void drawBoneRecursive(const std::shared_ptr<Bone>& bone, GLuint shaderProgram, const glm::mat4& parentMatrix) {
    glm::mat4 globalMatrix = parentMatrix * bone->getLocalTransform();

    if (bone->mesh) {
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(globalMatrix));
        bone->mesh->draw();
    }

    for (const auto& child : bone->children) {
        drawBoneRecursive(child, shaderProgram, globalMatrix);
    }
}

void AnimatedModel::draw(GLuint shaderProgram, const glm::mat4& worldTransform) const {
    for (const auto& rootBone : rootBones) {
        drawBoneRecursive(rootBone, shaderProgram, worldTransform);
    }
}