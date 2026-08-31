#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/version.h>

#include <cstdio>
#include <cstring>

int main()
{
    Assimp::Importer importer;

    if (aiGetVersionMajor() != 6 || !importer.IsExtensionSupported(".obj") ||
        !importer.IsExtensionSupported(".fbx") || !importer.IsExtensionSupported(".gltf") ||
        !importer.IsExtensionSupported(".glb") || importer.IsExtensionSupported(".3ds"))
    {
        std::fprintf(stderr, "Assimp importer registry does not match Vanguard's configuration.\n");
        return 1;
    }

    constexpr char obj[] =
        "o VanguardTriangle\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n";

    const aiScene* scene = importer.ReadFileFromMemory(
        obj, std::strlen(obj), aiProcess_Triangulate | aiProcess_ValidateDataStructure, "obj");

    if (scene == nullptr || scene->mNumMeshes != 1 || scene->mMeshes[0]->mNumVertices != 3 ||
        scene->mMeshes[0]->mNumFaces != 1 || scene->mMeshes[0]->mFaces[0].mNumIndices != 3)
    {
        std::fprintf(stderr, "Assimp OBJ memory import failed: %s\n", importer.GetErrorString());
        return 2;
    }

    std::printf(
        "Assimp %u.%u.%u: OBJ/FBX/glTF/GLB registry and OBJ parse verified.\n",
        aiGetVersionMajor(), aiGetVersionMinor(), aiGetVersionPatch());
    return 0;
}
