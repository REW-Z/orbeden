#include "Runtime/Object/Skybox.h"

OBJECT_TYPE_IMPLEMENT(Skybox, Object)

bool Skybox::ValidateFaces(std::string& error) const
{
    Texture2D* faces[] = {right.Get(), left.Get(), top.Get(), bottom.Get(), front.Get(), back.Get()};
    for (int32 index = 0; index < 6; ++index)
    {
        Texture2D* texture = faces[index];
        if (!texture || texture->width <= 0 || texture->width != texture->height
            || texture->channels < 3 || texture->channels > 4
            || texture->pixels.size() != static_cast<usize>(texture->width) * texture->height * texture->channels
            || (index != 0 && (texture->width != faces[0]->width
                || texture->channels != faces[0]->channels || texture->colorSpace != faces[0]->colorSpace)))
        {
            error = "Skybox faces must be complete square RGB/RGBA textures with matching size and color space.";
            return false;
        }
    }
    return true;
}

