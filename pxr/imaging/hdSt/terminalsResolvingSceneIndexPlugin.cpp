//
// Copyright 2022 Pixar
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
#include "terminalsResolvingSceneIndexPlugin.h"

#include "pxr/imaging/hdsi/terminalsResolvingSceneIndex.h"

#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"
#include "pxr/imaging/hio/glslfx.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (mtlx)
    ((sceneIndexPluginName, "HdSt_TerminalsResolvingSceneIndexPlugin")));

static const char* const _pluginDisplayName = "GL";

TF_REGISTRY_FUNCTION(TfType)
{
    HdSceneIndexPluginRegistry::Define<
        HdSt_TerminalsResolvingSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        _pluginDisplayName, _tokens->sceneIndexPluginName, nullptr,
        insertionPhase, HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

HdSt_TerminalsResolvingSceneIndexPlugin::
    HdSt_TerminalsResolvingSceneIndexPlugin()
    = default;

HdSt_TerminalsResolvingSceneIndexPlugin::
    ~HdSt_TerminalsResolvingSceneIndexPlugin()
    = default;

HdSceneIndexBaseRefPtr
HdSt_TerminalsResolvingSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr& inputSceneIndex,
    const HdContainerDataSourceHandle& inputArgs)
{
    TF_UNUSED(inputArgs);
    return HdsiTerminalsResolvingSceneIndex::New(
        inputSceneIndex,
        { HioGlslfxTokens->glslfx
#ifdef PXR_MATERIALX_SUPPORT_ENABLED
          , _tokens->mtlx
#endif
        });
}

PXR_NAMESPACE_CLOSE_SCOPE
