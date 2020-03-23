//
// Copyright 2019 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include <GL/glew.h>
#include "pxr/imaging/hgiGL/conversions.h"
#include "pxr/imaging/hgiGL/diagnostic.h"
#include "pxr/imaging/hgiGL/immediateCommandBuffer.h"
#include "pxr/imaging/hgiGL/blitEncoder.h"
#include "pxr/imaging/hgiGL/graphicsEncoder.h"
#include "pxr/imaging/hgiGL/texture.h"
#include "pxr/imaging/hgi/graphicsEncoderDesc.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/trace/trace.h"
#include <memory>

PXR_NAMESPACE_OPEN_SCOPE

struct HgiGLDescriptorCacheItem {
    HgiGraphicsEncoderDesc descriptor;
    HgiTextureHandle depthTexture;
    uint32_t framebuffer = 0;
}; 

std::ostream& operator<<(
    std::ostream& out,
    const HgiGLImmediateCommandBuffer& cmdBuf)
{
    out << "HgiGLImmediateCommandBuffer: {"
        << "descriptor cache: { ";

    for (HgiGLDescriptorCacheItem const * d : cmdBuf._descriptorCache) {
        out << d->descriptor;
    }

    out << "}}";
    return out;
}


static HgiGLDescriptorCacheItem*
_CreateDescriptorCacheItem(const HgiGraphicsEncoderDesc& desc)
{
    HgiGLDescriptorCacheItem* dci = new HgiGLDescriptorCacheItem();
    dci->descriptor = desc;
    dci->depthTexture = desc.depthTexture;

    // Create framebuffer
    glCreateFramebuffers(1, &dci->framebuffer);

    // Bind color attachments
    size_t numColorAttachments = desc.colorAttachmentDescs.size();
    std::vector<GLenum> drawBuffers(numColorAttachments);

    TF_VERIFY(desc.colorTextures.size() == numColorAttachments,
        "Number of attachment descriptors and textures don't match");
    
    //
    // Color attachments
    //
    for (size_t i=0; i<numColorAttachments; i++) {
        HgiGLTexture* glTexture = static_cast<HgiGLTexture*>(
            desc.colorTextures[i].Get());

        if (!TF_VERIFY(glTexture, "Invalid attachment texture")) {
            continue;
        }

        uint32_t textureName = glTexture->GetTextureId();
        if (!TF_VERIFY(glIsTexture(textureName), "Attachment not a texture")) {
            continue;
        }

        glNamedFramebufferTexture(
            dci->framebuffer,
            GL_COLOR_ATTACHMENT0 + i,
            textureName,
            /*level*/ 0);

        drawBuffers[i] = GL_COLOR_ATTACHMENT0 + i;
    }

    glNamedFramebufferDrawBuffers(
        dci->framebuffer,
        numColorAttachments,
        drawBuffers.data());

    //
    // Depth attachment
    //
    HgiTextureHandle depthTex = desc.depthTexture;
    if (depthTex) {
        HgiGLTexture* glTexture = static_cast<HgiGLTexture*>(depthTex.Get());

        uint32_t textureName = glTexture->GetTextureId();

        if (TF_VERIFY(glIsTexture(textureName), "Attachment not a texture")) {
            glNamedFramebufferTexture(
                dci->framebuffer,
                GL_DEPTH_ATTACHMENT,
                textureName, 
                0); // level
        }
    }

    // Note that if color or depth is multi-sample, they both have to be for GL.
    GLenum status = glCheckNamedFramebufferStatus(
        dci->framebuffer, 
        GL_FRAMEBUFFER);
    TF_VERIFY(status == GL_FRAMEBUFFER_COMPLETE);

    HGIGL_POST_PENDING_GL_ERRORS();
    return dci;
}

static void
_DestroyDescriptorCacheItem(HgiGLDescriptorCacheItem* dci)
{
    if (dci->framebuffer && glIsFramebuffer(dci->framebuffer)) {
        glDeleteFramebuffers(1, &dci->framebuffer);
        dci->framebuffer = 0;
    }

    delete dci;
    HGIGL_POST_PENDING_GL_ERRORS();
}

static HgiGLDescriptorCacheItem*
_AcquireDescriptorCacheItem(
    HgiGraphicsEncoderDesc const& desc,
    HgiGLDescriptorCacheVec& descriptorCache)
{
    // We keep a small cache of descriptor / framebuffer combos since it is 
    // potentially an expensive state change to attach textures to GL FBs.

    HgiGLDescriptorCacheItem* dci = nullptr;

    // Look for our framebuffer in cache
    for (size_t i=0; i<descriptorCache.size(); i++) {
        HgiGLDescriptorCacheItem* item = descriptorCache[i];
        if (desc == item->descriptor) {
            // If the GL context is changed we cannot re-use the framebuffer as
            // framebuffers cannot be shared between contexts.
            if (glIsFramebuffer(item->framebuffer)) {
                dci = item;

                // Move descriptor to end of 'LRU cache' as it is still used.
                if (i < descriptorCache.size()) {
                    descriptorCache.erase(descriptorCache.begin() + i);
                    descriptorCache.push_back(dci);
                }
            }
            break;
        }
    }

    // Create a new descriptor cache item if it was not found
    if (!dci) {
        dci = _CreateDescriptorCacheItem(desc);
        descriptorCache.push_back(dci);

        // Destroy oldest descriptor / FB in LRU cache vector.
        // The size of the cache is small enough and we only store ptrs so we
        // use a vector instead of a linked list LRU.
        const size_t descriptorLRUsize = 32;
        if (descriptorCache.size() == descriptorLRUsize) {
            _DestroyDescriptorCacheItem(descriptorCache.front());
            descriptorCache.erase(descriptorCache.begin());
        }
    }

    return dci;
}

static void
_BindFramebuffer(HgiGLDescriptorCacheItem* dci)
{
    glBindFramebuffer(GL_FRAMEBUFFER, dci->framebuffer);

    bool blendEnabled = false;

    // Apply LoadOps
    for (size_t i=0; i<dci->descriptor.colorAttachmentDescs.size(); i++) {
        HgiAttachmentDesc const& colorAttachment =
            dci->descriptor.colorAttachmentDescs[i];

        if (colorAttachment.loadOp == HgiAttachmentLoadOpClear) {
            glClearBufferfv(GL_COLOR, i, colorAttachment.clearValue.data());
        }

        blendEnabled |= colorAttachment.blendEnabled;

        GLenum srcColor = HgiGLConversions::GetBlendFactor(
            colorAttachment.srcColorBlendFactor);
        GLenum dstColor = HgiGLConversions::GetBlendFactor(
            colorAttachment.dstColorBlendFactor);

        GLenum srcAlpha = HgiGLConversions::GetBlendFactor(
            colorAttachment.srcAlphaBlendFactor);
        GLenum dstAlpha = HgiGLConversions::GetBlendFactor(
            colorAttachment.dstAlphaBlendFactor);

        GLenum colorOp = HgiGLConversions::GetBlendEquation(
            colorAttachment.colorBlendOp);
        GLenum alphaOp = HgiGLConversions::GetBlendEquation(
            colorAttachment.alphaBlendOp);

        glBlendFuncSeparatei(i, srcColor, dstColor, srcAlpha, dstAlpha);
        glBlendEquationSeparatei(i, colorOp, alphaOp);
    }

    HgiAttachmentDesc const& depthAttachment =
        dci->descriptor.depthAttachmentDesc;
    if (dci->depthTexture &&
        depthAttachment.loadOp == HgiAttachmentLoadOpClear) {
        glClearBufferfv(GL_DEPTH, 0, depthAttachment.clearValue.data());
    }

    // Setup blending
    if (blendEnabled) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }

    HGIGL_POST_PENDING_GL_ERRORS();
}

HgiGLImmediateCommandBuffer::HgiGLImmediateCommandBuffer()
{
}

HgiGLImmediateCommandBuffer::~HgiGLImmediateCommandBuffer()
{
    for (HgiGLDescriptorCacheItem* dci : _descriptorCache) {
        _DestroyDescriptorCacheItem(dci);
    }
}

HgiGraphicsEncoderUniquePtr
HgiGLImmediateCommandBuffer::CreateGraphicsEncoder(
    HgiGraphicsEncoderDesc const& desc)
{
    TRACE_FUNCTION();

    // XXX This check should be removed once the tasks have switched over to
    // Hgi so that the PresentTask can render to framebuffer (see XXX below).
    if (!desc.HasAttachments()) {
        // XXX For now we do not emit a warning because we have to many
        // pieces that do not yet use Hgi fully.
        // TF_WARN("Encoder descriptor incomplete");
        return nullptr;
    }

    const size_t maxColorAttachments = 8;
    if (!TF_VERIFY(desc.colorAttachmentDescs.size() <= maxColorAttachments,
        "Too many color attachments for OpenGL frambuffer"))
    {
        return nullptr;
    }

    // XXX With other API's like Metal and Vulkan having a encoder without
    // attachments doesn't make a lot of sense.
    // For OpenGL we will need this for Hgi transition to sometimes assume that
    // no-attachments means rendering into the globally bound GL framebuffer.
    // Once HgiInterop is fully in place in the PresentTask we should enable
    // the error below when there are not attachments.
    // XXX TF_VERIFY(desc.HasAttachments());
    if (desc.HasAttachments()) {
        HgiGLDescriptorCacheItem* dci = 
            _AcquireDescriptorCacheItem(desc, _descriptorCache);
        _BindFramebuffer(dci);
    }

    HgiGLGraphicsEncoder* encoder(new HgiGLGraphicsEncoder(desc));

    return HgiGraphicsEncoderUniquePtr(encoder);
}

HgiBlitEncoderUniquePtr
HgiGLImmediateCommandBuffer::CreateBlitEncoder()
{
    return HgiBlitEncoderUniquePtr(new HgiGLBlitEncoder(this));
}

void
HgiGLImmediateCommandBuffer::BlockUntilCompleted()
{
    // On other APIs this would be an equivalent of a glFinish()
    //glFinish();
}

void
HgiGLImmediateCommandBuffer::BlockUntilSubmitted()
{
    // On other APIs this would be an equivalent of a glFlush()
    //glFlush();
}

PXR_NAMESPACE_CLOSE_SCOPE
