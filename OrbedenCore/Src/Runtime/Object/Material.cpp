#include "Runtime/Object/Material.h"
#include "Runtime/Object/Texture2D.h"

OBJECT_TYPE_IMPLEMENT(Material, Object)

namespace
{
    //查找可写纹理槽
    MaterialTextureSlot* FindTextureSlot(List<MaterialTextureSlot>& slots, const std::string& slotName)
    {
        for (MaterialTextureSlot& slot : slots)
        {
            if (slot.name == slotName) return &slot;
        }

        return nullptr;
    }

    //查找只读纹理槽
    const MaterialTextureSlot* FindTextureSlot(const List<MaterialTextureSlot>& slots, const std::string& slotName)
    {
        for (const MaterialTextureSlot& slot : slots)
        {
            if (slot.name == slotName) return &slot;
        }

        return nullptr;
    }

    //查找可写颜色槽
    MaterialColorSlot* FindColorSlot(List<MaterialColorSlot>& slots, const std::string& slotName)
    {
        for (MaterialColorSlot& slot : slots)
        {
            if (slot.name == slotName) return &slot;
        }

        return nullptr;
    }

    //查找只读颜色槽
    const MaterialColorSlot* FindColorSlot(const List<MaterialColorSlot>& slots, const std::string& slotName)
    {
        for (const MaterialColorSlot& slot : slots)
        {
            if (slot.name == slotName) return &slot;
        }

        return nullptr;
    }

    //查找可写浮点槽
    MaterialFloatSlot* FindFloatSlot(List<MaterialFloatSlot>& slots, const std::string& slotName)
    {
        for (MaterialFloatSlot& slot : slots)
        {
            if (slot.name == slotName) return &slot;
        }

        return nullptr;
    }

    //查找只读浮点槽
    const MaterialFloatSlot* FindFloatSlot(const List<MaterialFloatSlot>& slots, const std::string& slotName)
    {
        for (const MaterialFloatSlot& slot : slots)
        {
            if (slot.name == slotName) return &slot;
        }

        return nullptr;
    }
}

void Material::SetTexture(const std::string& slotName, Texture2D* texture)
{
    if (slotName.empty()) return;

    MaterialTextureSlot* slot = FindTextureSlot(textureSlots, slotName);
    if (!slot)
    {
        MaterialTextureSlot created;
        created.name = slotName;
        textureSlots.push_back(created);
        slot = &textureSlots.back();
    }

    slot->texture.Set(texture);
    revision++;
}

void Material::SetTexture(const std::string& slotName, const StringId& textureId)
{
    if (slotName.empty()) return;

    MaterialTextureSlot* slot = FindTextureSlot(textureSlots, slotName);
    if (!slot)
    {
        MaterialTextureSlot created;
        created.name = slotName;
        textureSlots.push_back(created);
        slot = &textureSlots.back();
    }

    slot->texture.SetInstanceId(textureId);
    revision++;
}

Texture2D* Material::GetTexture(const std::string& slotName) const
{
    const MaterialTextureSlot* slot = FindTextureSlot(textureSlots, slotName);
    return slot ? slot->texture.Get() : nullptr;
}

bool Material::HasTexture(const std::string& slotName) const
{
    const MaterialTextureSlot* slot = FindTextureSlot(textureSlots, slotName);
    return slot && slot->texture.GetInstanceId().IsValid();
}

void Material::ClearTexture(const std::string& slotName)
{
    for (usize index = 0; index < textureSlots.size(); ++index)
    {
        if (textureSlots[index].name != slotName) continue;

        textureSlots.erase(textureSlots.begin() + static_cast<isize>(index));
        revision++;
        return;
    }
}

void Material::SetColor(const std::string& slotName, const color4& value)
{
    if (slotName.empty()) return;

    MaterialColorSlot* slot = FindColorSlot(colorSlots, slotName);
    if (!slot)
    {
        MaterialColorSlot created;
        created.name = slotName;
        colorSlots.push_back(created);
        slot = &colorSlots.back();
    }

    slot->value = value;
    revision++;
}

color4 Material::GetColor(const std::string& slotName, const color4& defaultValue) const
{
    const MaterialColorSlot* slot = FindColorSlot(colorSlots, slotName);
    return slot ? slot->value : defaultValue;
}

bool Material::HasColor(const std::string& slotName) const
{
    return FindColorSlot(colorSlots, slotName) != nullptr;
}

void Material::ClearColor(const std::string& slotName)
{
    for (usize index = 0; index < colorSlots.size(); ++index)
    {
        if (colorSlots[index].name != slotName) continue;

        colorSlots.erase(colorSlots.begin() + static_cast<isize>(index));
        revision++;
        return;
    }
}

void Material::SetFloat(const std::string& slotName, float32 value)
{
    if (slotName.empty()) return;

    MaterialFloatSlot* slot = FindFloatSlot(floatSlots, slotName);
    if (!slot)
    {
        MaterialFloatSlot created;
        created.name = slotName;
        floatSlots.push_back(created);
        slot = &floatSlots.back();
    }

    slot->value = value;
    revision++;
}

float32 Material::GetFloat(const std::string& slotName, float32 defaultValue) const
{
    const MaterialFloatSlot* slot = FindFloatSlot(floatSlots, slotName);
    return slot ? slot->value : defaultValue;
}

bool Material::HasFloat(const std::string& slotName) const
{
    return FindFloatSlot(floatSlots, slotName) != nullptr;
}

void Material::ClearFloat(const std::string& slotName)
{
    for (usize index = 0; index < floatSlots.size(); ++index)
    {
        if (floatSlots[index].name != slotName) continue;

        floatSlots.erase(floatSlots.begin() + static_cast<isize>(index));
        revision++;
        return;
    }
}

uint64 Material::GetRevision() const
{
    return revision;
}
