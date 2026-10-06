#pragma once
#include "anyapi_item_catalog.h"
#include "item_mesh_preview.h"
namespace itemcatalog {
inline std::vector<uint8_t> preview(const std::filesystem::path& game,const Entry& entry){itempreview::Mesh combined;for(auto& name:mesh_paths(entry)){std::filesystem::path path=name;if(path.is_absolute())throw std::runtime_error("Absolute item mesh path");for(auto& part:path)if(part==L"..")throw std::runtime_error("Item mesh path traversal");auto data=read(game/L"rom"/path);auto mesh=itempreview::parse(std::vector<uint8_t>(data.begin(),data.end()));auto base=uint32_t(combined.vertices.size());combined.vertices.insert(combined.vertices.end(),mesh.vertices.begin(),mesh.vertices.end());for(auto i:mesh.indices)combined.indices.push_back(base+i);}if(combined.indices.empty())throw std::runtime_error("Item preview has no geometry");return itempreview::render(combined);}
}
