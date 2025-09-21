//----------------------------------------------------------------------------//
//                                                                            //
// ozz-animation is hosted at http://github.com/guillaumeblanc/ozz-animation  //
// and distributed under the MIT License (MIT).                               //
//                                                                            //
// Copyright (c) Guillaume Blanc                                              //
//                                                                            //
// Permission is hereby granted, free of charge, to any person obtaining a    //
// copy of this software and associated documentation files (the "Software"), //
// to deal in the Software without restriction, including without limitation  //
// the rights to use, copy, modify, merge, publish, distribute, sublicense,   //
// and/or sell copies of the Software, and to permit persons to whom the      //
// Software is furnished to do so, subject to the following conditions:       //
//                                                                            //
// The above copyright notice and this permission notice shall be included in //
// all copies or substantial portions of the Software.                        //
//                                                                            //
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR //
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   //
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    //
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER //
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    //
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        //
// DEALINGS IN THE SOFTWARE.                                                  //
//                                                                            //
//----------------------------------------------------------------------------//

#include "mesh.h"

#include "ozz/base/containers/vector_archive.h"
#include "ozz/base/io/archive.h"
#include "ozz/base/maths/math_archive.h"
#include "ozz/base/maths/simd_math_archive.h"
#include "ozz/base/memory/allocator.h"

namespace ozz {
namespace io {

namespace {
void SaveString(OArchive& _archive, const std::string& value) {
  const uint32_t size = static_cast<uint32_t>(value.size());
  _archive << size;
  if (size > 0) {
    _archive << ozz::io::MakeArray(value.data(), size);
  }
}

void LoadString(IArchive& _archive, std::string* value) {
  uint32_t size = 0;
  _archive >> size;
  value->resize(size);
  if (size > 0) {
    _archive >> ozz::io::MakeArray(value->data(), size);
  }
}
}  // namespace

void Extern<sample::XRayMeshMetadata>::Save(
    OArchive& _archive, const sample::XRayMeshMetadata* _metadata,
    size_t _count) {
  for (size_t i = 0; i < _count; ++i) {
    const sample::XRayMeshMetadata& data = _metadata[i];
    SaveString(_archive, data.texture_path);
    SaveString(_archive, data.shader_name);
    _archive << data.texture_link_present;
    _archive << data.texture_link;
    _archive << data.shader_link_present;
    _archive << data.shader_link;
    _archive << data.original_vertex_count;
    _archive << data.original_face_count;
    _archive << data.ogf_type;
    const uint32_t lod_count = static_cast<uint32_t>(data.lod_visuals.size());
    _archive << lod_count;
    for (const std::string& entry : data.lod_visuals) {
      SaveString(_archive, entry);
    }
    _archive << data.lod_data;
    _archive << data.progressive_collapse_count;
    _archive << data.progressive_data;
    _archive << data.child_visual_links;
  }
}

void Extern<sample::XRayMeshMetadata>::Load(IArchive& _archive,
                                            sample::XRayMeshMetadata* _metadata,
                                            size_t _count, uint32_t _version) {
  (void)_version;
  for (size_t i = 0; i < _count; ++i) {
    sample::XRayMeshMetadata& data = _metadata[i];
    LoadString(_archive, &data.texture_path);
    LoadString(_archive, &data.shader_name);
    _archive >> data.texture_link_present;
    _archive >> data.texture_link;
    _archive >> data.shader_link_present;
    _archive >> data.shader_link;
    _archive >> data.original_vertex_count;
    _archive >> data.original_face_count;
    _archive >> data.ogf_type;
    uint32_t lod_count = 0;
    _archive >> lod_count;
    data.lod_visuals.clear();
    data.lod_visuals.reserve(lod_count);
    for (uint32_t lod_index = 0; lod_index < lod_count; ++lod_index) {
      std::string entry;
      LoadString(_archive, &entry);
      data.lod_visuals.push_back(std::move(entry));
    }
    _archive >> data.lod_data;
    _archive >> data.progressive_collapse_count;
    _archive >> data.progressive_data;
    _archive >> data.child_visual_links;
  }
}

void Extern<sample::Mesh::Part>::Save(OArchive& _archive,
                                      const sample::Mesh::Part* _parts,
                                      size_t _count) {
  for (size_t i = 0; i < _count; ++i) {
    const sample::Mesh::Part& part = _parts[i];
    _archive << part.positions;
    _archive << part.normals;
    _archive << part.tangents;
    _archive << part.uvs;
    _archive << part.colors;
    _archive << part.joint_indices;
    _archive << part.joint_weights;
  }
}

void Extern<sample::Mesh::Part>::Load(IArchive& _archive,
                                      sample::Mesh::Part* _parts, size_t _count,
                                      uint32_t _version) {
  (void)_version;
  for (size_t i = 0; i < _count; ++i) {
    sample::Mesh::Part& part = _parts[i];
    _archive >> part.positions;
    _archive >> part.normals;
    _archive >> part.tangents;
    _archive >> part.uvs;
    _archive >> part.colors;
    _archive >> part.joint_indices;
    _archive >> part.joint_weights;
  }
}

void Extern<sample::Mesh>::Save(OArchive& _archive, const sample::Mesh* _meshes,
                                size_t _count) {
  for (size_t i = 0; i < _count; ++i) {
    const sample::Mesh& mesh = _meshes[i];
    _archive << mesh.parts;
    _archive << mesh.triangle_indices;
    _archive << mesh.joint_remaps;
    _archive << mesh.inverse_bind_poses;
    _archive << mesh.xray_metadata;
  }
}

void Extern<sample::Mesh>::Load(IArchive& _archive, sample::Mesh* _meshes,
                                size_t _count, uint32_t _version) {
  (void)_version;
  for (size_t i = 0; i < _count; ++i) {
    sample::Mesh& mesh = _meshes[i];
    _archive >> mesh.parts;
    _archive >> mesh.triangle_indices;
    _archive >> mesh.joint_remaps;
    _archive >> mesh.inverse_bind_poses;
    _archive >> mesh.xray_metadata;
  }
}
}  // namespace io
}  // namespace ozz
