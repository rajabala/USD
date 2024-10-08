//
// Copyright 2020 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/imaging/hdSt/textureObject.h"

#include "pxr/imaging/hdSt/assetUvTextureCpuData.h"
#include "pxr/imaging/hdSt/fieldTextureCpuData.h"
#include "pxr/imaging/hdSt/ptexTextureObject.h"
#include "pxr/imaging/hdSt/resourceRegistry.h"
#include "pxr/imaging/hdSt/textureCpuData.h"
#include "pxr/imaging/hdSt/textureObjectRegistry.h"
#include "pxr/imaging/hdSt/subtextureIdentifier.h"
#include "pxr/imaging/hdSt/fieldSubtextureIdentifier.h"
#include "pxr/imaging/hdSt/textureIdentifier.h"
#include "pxr/imaging/hdSt/tokens.h"
#include "pxr/imaging/hdSt/udimTextureObject.h"

#include "pxr/imaging/hgi/hgi.h"
#include "pxr/imaging/hgi/blitCmds.h"

#include "pxr/imaging/hio/fieldTextureData.h"

#include "pxr/usd/ar/resolver.h"

PXR_NAMESPACE_OPEN_SCOPE

///////////////////////////////////////////////////////////////////////////////
// HdStTextureObject

HdStTextureObject::HdStTextureObject(
    const HdStTextureIdentifier &textureId,
    HdSt_TextureObjectRegistry * const textureObjectRegistry)
  : _textureObjectRegistry(textureObjectRegistry)
  , _textureId(textureId)
  , _targetMemory(0)
{
}

void
HdStTextureObject::SetTargetMemory(const size_t targetMemory)
{
    if (_targetMemory == targetMemory) {
        return;
    }
    _targetMemory = targetMemory;
    _textureObjectRegistry->MarkTextureObjectDirty(shared_from_this());
}

HdStResourceRegistry*
HdStTextureObject::_GetResourceRegistry() const
{
    if (!TF_VERIFY(_textureObjectRegistry)) {
        return nullptr;
    }

    HdStResourceRegistry* const registry =
        _textureObjectRegistry->GetResourceRegistry();
    TF_VERIFY(registry);

    return registry;
}

Hgi *
HdStTextureObject::_GetHgi() const
{
    HdStResourceRegistry* const registry = _GetResourceRegistry();
    if (!TF_VERIFY(registry)) {
        return nullptr;
    }

    Hgi * const hgi = registry->GetHgi();
    TF_VERIFY(hgi);

    return hgi;
}

void
HdStTextureObject::_AdjustTotalTextureMemory(
    const int64_t memDiff)
{
    if (TF_VERIFY(_textureObjectRegistry)) {
        _textureObjectRegistry->AdjustTotalTextureMemory(memDiff);
    }
}

void
HdStTextureObject::_AddToTotalTextureMemory(
    const HgiTextureHandle &texture)
{
    if (texture) {
        const int64_t m = static_cast<int64_t>(
            texture->GetByteSizeOfResource());
        _AdjustTotalTextureMemory(m);
    }
}

void
HdStTextureObject::_SubtractFromTotalTextureMemory(
    const HgiTextureHandle &texture)
{
    if (texture) {
        const int64_t m = static_cast<int64_t>(
            texture->GetByteSizeOfResource());
        _AdjustTotalTextureMemory(-m);
    }
}

HdStTextureObject::~HdStTextureObject() = default;

///////////////////////////////////////////////////////////////////////////////
// Helpers

std::string
HdStTextureObject::_GetDebugName(const HdStTextureIdentifier &textureId) const
{
    const std::string &filePath = textureId.GetFilePath().GetString();
    const HdStSubtextureIdentifier * const subId =
        textureId.GetSubtextureIdentifier();

    if (!subId) {
        return filePath;
    }

    if (const HdStOpenVDBAssetSubtextureIdentifier * const vdbSubId =
            dynamic_cast<const HdStOpenVDBAssetSubtextureIdentifier*>(subId)) {
        return
            filePath + " - "
            + vdbSubId->GetFieldName().GetString();
    }

    if (const HdStField3DAssetSubtextureIdentifier * const f3dSubId =
            dynamic_cast<const HdStField3DAssetSubtextureIdentifier*>(subId)) {
        return
            filePath + " - "
            + f3dSubId->GetFieldName().GetString() + " "
            + std::to_string(f3dSubId->GetFieldIndex()) + " "
            + f3dSubId->GetFieldPurpose().GetString();
    }

    if (const HdStAssetUvSubtextureIdentifier * const assetUvSubId =
            dynamic_cast<const HdStAssetUvSubtextureIdentifier*>(subId)) {
        return
            filePath
            + " - flipVertically="
            + std::to_string(int(assetUvSubId->GetFlipVertically()))
            + " - premultiplyAlpha="
            + std::to_string(int(assetUvSubId->GetPremultiplyAlpha()))
            + " - sourceColorSpace="
            + assetUvSubId->GetSourceColorSpace().GetString();
    }

    if (const HdStPtexSubtextureIdentifier * const ptexSubId =
            dynamic_cast<const HdStPtexSubtextureIdentifier*>(subId)) {
        return
            filePath
            + " - premultiplyAlpha="
            + std::to_string(int(ptexSubId->GetPremultiplyAlpha()));
    }

    if (const HdStUdimSubtextureIdentifier * const udimSubId =
            dynamic_cast<const HdStUdimSubtextureIdentifier*>(subId)) {
        return
            filePath +
            + " - premultiplyAlpha="
            + std::to_string(int(udimSubId->GetPremultiplyAlpha()))
            + " - sourceColorSpace="
            + udimSubId->GetSourceColorSpace().GetString();
    }

    return filePath + " - unknown subtexture identifier";
}

// Read from the HdStSubtextureIdentifier whether we need
// to pre-multiply the texture by alpha
//
bool
HdStTextureObject::_GetPremultiplyAlpha(
        const HdStSubtextureIdentifier * const subId) const
{
    switch(GetTextureType()) {
    case HdStTextureType::Uv:
        if (const HdStAssetUvSubtextureIdentifier* const uvSubId =
            dynamic_cast<const HdStAssetUvSubtextureIdentifier *>(subId)) {
            return uvSubId->GetPremultiplyAlpha();
        }
        return false;
    case HdStTextureType::Ptex:
        if (const HdStPtexSubtextureIdentifier* const ptexSubId =
            dynamic_cast<const HdStPtexSubtextureIdentifier *>(subId)) {
        return ptexSubId->GetPremultiplyAlpha();
        }
        return false;
    case HdStTextureType::Udim:
        if (const HdStUdimSubtextureIdentifier* const udimSubId =
                dynamic_cast<const HdStUdimSubtextureIdentifier *>(subId)) {
            return udimSubId->GetPremultiplyAlpha();
        }
        return false;
    default:
        return false;
    }
}

// Read from the HdStSubtextureIdentifier its source color space
//
HioImage::SourceColorSpace
HdStTextureObject::_GetSourceColorSpace(
        const HdStSubtextureIdentifier * const subId) const
{
    TfToken sourceColorSpace;
    switch(GetTextureType()) {
    case HdStTextureType::Uv:
        if (const HdStAssetUvSubtextureIdentifier* const uvSubId =
            dynamic_cast<const HdStAssetUvSubtextureIdentifier *>(subId)) {
            sourceColorSpace = uvSubId->GetSourceColorSpace();
        }
        break;
    case HdStTextureType::Udim:
        if (const HdStUdimSubtextureIdentifier* const udimSubId =
                dynamic_cast<const HdStUdimSubtextureIdentifier *>(subId)) {
            sourceColorSpace = udimSubId->GetSourceColorSpace();
        }
        break;
    default:
        break;
    }

    if (sourceColorSpace == HdStTokens->sRGB) {
        return HioImage::SRGB;
    }
    if (sourceColorSpace == HdStTokens->raw) {
        return HioImage::Raw;
    }
    return HioImage::Auto;
}

///////////////////////////////////////////////////////////////////////////////
// Uv texture

HdStUvTextureObject::HdStUvTextureObject(
    const HdStTextureIdentifier &textureId,
    HdSt_TextureObjectRegistry * textureObjectRegistry)
  : HdStTextureObject(textureId, textureObjectRegistry)
  , _wrapParameters{HdWrapNoOpinion, HdWrapNoOpinion}
{
}


HdStTextureType
HdStUvTextureObject::GetTextureType() const
{
    return HdStTextureType::Uv;
}

HdStUvTextureObject::~HdStUvTextureObject()
{
    _DestroyTexture();
}

void
HdStUvTextureObject::_SetWrapParameters(
    const std::pair<HdWrap, HdWrap> &wrapParameters)
{
    _wrapParameters = wrapParameters;
}

void
HdStUvTextureObject::_SetCpuData(
    std::unique_ptr<HdStTextureCpuData> &&cpuData)
{
    _cpuData = std::move(cpuData);
}

HdStTextureCpuData *
HdStUvTextureObject::_GetCpuData() const
{
    return _cpuData.get();
}

void
HdStUvTextureObject::_CreateTexture(const HgiTextureDesc &desc)
{
    Hgi * const hgi = _GetHgi();
    if (!TF_VERIFY(hgi)) {
        return;
    }

    _DestroyTexture();

    _gpuTexture = hgi->CreateTexture(desc);
    _AddToTotalTextureMemory(_gpuTexture);
}

void
HdStUvTextureObject::_GenerateMipmaps()
{
    HdStResourceRegistry * const registry = _GetResourceRegistry();
    if (!TF_VERIFY(registry)) {
        return;
    }

    if (!_gpuTexture) {
        return;
    }

    HgiBlitCmds* const blitCmds = registry->GetGlobalBlitCmds();
    blitCmds->GenerateMipMaps(_gpuTexture);
}

void
HdStUvTextureObject::_DestroyTexture()
{
    if (Hgi * hgi = _GetHgi()) {
        _SubtractFromTotalTextureMemory(_gpuTexture);
        hgi->DestroyTexture(&_gpuTexture);
    }
}

///////////////////////////////////////////////////////////////////////////////
// Uv asset texture

// Read from the HdStAssetUvSubtextureIdentifier whether we need
// to flip the image.
//
// This is to support the legacy HwUvTexture_1 shader node which has the
// vertical orientation opposite to UsdUvTexture.
//
static
HioImage::ImageOriginLocation
_GetImageOriginLocation(const HdStSubtextureIdentifier * const subId)
{
    using SubId = const HdStAssetUvSubtextureIdentifier;

    if (SubId* const uvSubId = dynamic_cast<SubId*>(subId)) {
        if (uvSubId->GetFlipVertically()) {
            return HioImage::OriginUpperLeft;
        }
    }
    return HioImage::OriginLowerLeft;
}

HdStAssetUvTextureObject::HdStAssetUvTextureObject(
    const HdStTextureIdentifier &textureId,
    HdSt_TextureObjectRegistry * const textureObjectRegistry)
  : HdStUvTextureObject(textureId, textureObjectRegistry)
  , _valid(false)
{
}

HdStAssetUvTextureObject::~HdStAssetUvTextureObject() = default;

void
HdStAssetUvTextureObject::_Load()
{
    TRACE_FUNCTION();

    std::unique_ptr<HdStAssetUvTextureCpuData> cpuData =
        std::make_unique<HdStAssetUvTextureCpuData>(
            GetTextureIdentifier().GetFilePath(),
            GetTargetMemory(),
            _GetPremultiplyAlpha(
                GetTextureIdentifier().GetSubtextureIdentifier()),
            _GetImageOriginLocation(
                GetTextureIdentifier().GetSubtextureIdentifier()),
            HdStTextureObject::_GetSourceColorSpace(
                GetTextureIdentifier().GetSubtextureIdentifier()));
    _SetWrapParameters(cpuData->GetWrapInfo());
    _SetCpuData(std::move(cpuData));
}

void
HdStAssetUvTextureObject::_Commit()
{
    TRACE_FUNCTION();

    _DestroyTexture();
    _valid = false;

    if (HdStTextureCpuData * const cpuData = _GetCpuData()) {
        if (cpuData->IsValid()) {
            // Upload to GPU
            _CreateTexture(cpuData->GetTextureDesc());
            _valid = true;
            if (cpuData->GetGenerateMipmaps()) {
                _GenerateMipmaps();
            }
        }
    }

    if (!_valid) {
        // Create 1x1x1 black fallback texture.
        HgiTextureDesc textureDesc;
        textureDesc.debugName = "AssetUvTextureFallback";
        textureDesc.usage = HgiTextureUsageBitsShaderRead;
        textureDesc.type = HgiTextureType2D;
        textureDesc.dimensions = GfVec3i(1, 1, 1);
        textureDesc.format = HgiFormatUNorm8Vec4;
        textureDesc.mipLevels = 1;
        textureDesc.layerCount = 1;
        const unsigned char data[4] = {0, 0, 0, 255};
        textureDesc.initialData = &data[0];
        textureDesc.pixelsByteSize = 4 * sizeof(unsigned char);
        _CreateTexture(textureDesc);
    }

    // Free CPU memory after transfer to GPU
    _SetCpuData(nullptr);
}

bool
HdStAssetUvTextureObject::IsValid() const
{
    return _valid;
}

///////////////////////////////////////////////////////////////////////////////
// Field texture

// Compute transform mapping GfRange3d to unit box [0,1]^3
static
GfMatrix4d
_ComputeSamplingTransform(const GfRange3d &range)
{
    const GfVec3d size(range.GetSize());

    const GfVec3d scale(1.0 / size[0], 1.0 / size[1], 1.0 / size[2]);

    return
        // First map range so that min becomes (0,0,0)
        GfMatrix4d(1.0).SetTranslateOnly(-range.GetMin()) *
        // Then scale to unit box
        GfMatrix4d(1.0).SetScale(scale);
}

// Compute transform mapping bounding box to unit box [0,1]^3
static
GfMatrix4d
_ComputeSamplingTransform(const GfBBox3d &bbox)
{
    return
        // First map so that bounding box goes to its GfRange3d
        bbox.GetInverseMatrix() *
        // Then scale to unit box [0,1]^3
        _ComputeSamplingTransform(bbox.GetRange());
}

static
HioFieldTextureDataSharedPtr
_ComputeFieldTexData(
    const HdStTextureIdentifier &textureId,
    const size_t targetMemory)
{
    const std::string &filePath = textureId.GetFilePath().GetString();
    const HdStSubtextureIdentifier * const subId =
        textureId.GetSubtextureIdentifier();

    if (const HdStOpenVDBAssetSubtextureIdentifier * const vdbSubId =
            dynamic_cast<const HdStOpenVDBAssetSubtextureIdentifier*>(subId)) {
        if (vdbSubId->GetFieldIndex() != 0) {
            TF_WARN("Support of field index when reading OpenVDB file not yet "
                    "implemented (file: %s, field name: %s, field index: %d",
                    filePath.c_str(),
                    vdbSubId->GetFieldName().GetText(),
                    vdbSubId->GetFieldIndex());
        }
        return HioFieldTextureData::New(
                filePath,
                vdbSubId->GetFieldName(),
                0,
                std::string(),
                targetMemory);
    }

    if (const HdStField3DAssetSubtextureIdentifier * const f3dSubId =
            dynamic_cast<const HdStField3DAssetSubtextureIdentifier*>(subId)) {
        return HioFieldTextureData::New(
                filePath,
                f3dSubId->GetFieldName(),
                f3dSubId->GetFieldIndex(),
                f3dSubId->GetFieldPurpose(),
                targetMemory);
    }

    TF_CODING_ERROR("Unsupported field subtexture identifier");

    return nullptr;
}


HdStFieldTextureObject::HdStFieldTextureObject(
    const HdStTextureIdentifier &textureId,
    HdSt_TextureObjectRegistry * const textureObjectRegistry)
  : HdStTextureObject(textureId, textureObjectRegistry)
  , _valid(false)
{
}

HdStFieldTextureObject::~HdStFieldTextureObject()
{
    if (Hgi * hgi = _GetHgi()) {
        _SubtractFromTotalTextureMemory(_gpuTexture);
        hgi->DestroyTexture(&_gpuTexture);
    }
    _valid = false;
}

void
HdStFieldTextureObject::_Load()
{
    TRACE_FUNCTION();

    HioFieldTextureDataSharedPtr const texData = _ComputeFieldTexData(
        GetTextureIdentifier(),
        GetTargetMemory());

    if (!texData) {
        return;
    }

    texData->Read();

    _cpuData = std::make_unique<HdSt_FieldTextureCpuData>(
        texData,
        _GetDebugName(GetTextureIdentifier()));

    if (_cpuData->IsValid()) {
        if (_cpuData->GetTextureDesc().type != HgiTextureType3D) {
            TF_CODING_ERROR("Wrong texture type for field");
        }

        _bbox = texData->GetBoundingBox();
        _samplingTransform = _ComputeSamplingTransform(_bbox);
    } else {
        _bbox = GfBBox3d();
        _samplingTransform = GfMatrix4d(1.0);
    }

}

void
HdStFieldTextureObject::_Commit()
{
    TRACE_FUNCTION();

    Hgi * const hgi = _GetHgi();
    if (!hgi) {
        return;
    }

    // Free previously allocated texture
    _SubtractFromTotalTextureMemory(_gpuTexture);
    hgi->DestroyTexture(&_gpuTexture);
    _valid = false;

    // Upload to GPU only if we have valid CPU data
    if (_cpuData && _cpuData->IsValid()) {
        _gpuTexture = hgi->CreateTexture(_cpuData->GetTextureDesc());
        _valid = true;
    } else {
        // Create 1x1x1 black fallback texture.
        HgiTextureDesc textureDesc;
        textureDesc.debugName = "FieldTextureFallback";
        textureDesc.usage = HgiTextureUsageBitsShaderRead;
        textureDesc.format = HgiFormatUNorm8Vec4;
        textureDesc.type = HgiTextureType3D;
        textureDesc.dimensions = GfVec3i(1, 1, 1);
        textureDesc.layerCount = 1;
        textureDesc.mipLevels = 1;
        textureDesc.pixelsByteSize = 4 * sizeof(unsigned char);
        const unsigned char data[4] = {0, 0, 0, 255};
        textureDesc.initialData = &data[0];
        _gpuTexture = hgi->CreateTexture(textureDesc);
    }

    _AddToTotalTextureMemory(_gpuTexture);

    // Free CPU memory after transfer to GPU
    _cpuData.reset();
}

bool
HdStFieldTextureObject::IsValid() const
{
    return _valid;
}

HdStTextureType
HdStFieldTextureObject::GetTextureType() const
{
    return HdStTextureType::Field;
}

PXR_NAMESPACE_CLOSE_SCOPE
