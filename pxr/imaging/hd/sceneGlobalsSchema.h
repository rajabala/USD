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
//
////////////////////////////////////////////////////////////////////////

/* ************************************************************************** */
/* ** This file is generated by a script.  Do not edit directly.  Edit     ** */
/* ** defs.py or the (*)Schema.template.h files to make changes.           ** */
/* ************************************************************************** */

#ifndef PXR_IMAGING_HD_SCENE_GLOBALS_SCHEMA_H
#define PXR_IMAGING_HD_SCENE_GLOBALS_SCHEMA_H

#include "pxr/imaging/hd/api.h"

#include "pxr/imaging/hd/schema.h" 


#include "pxr/usd/sdf/path.h"
#include "pxr/imaging/hd/sceneIndex.h"

PXR_NAMESPACE_OPEN_SCOPE

//-----------------------------------------------------------------------------

#define HDSCENEGLOBALS_SCHEMA_TOKENS \
    (sceneGlobals) \
    (activeRenderSettingsPrim) \

TF_DECLARE_PUBLIC_TOKENS(HdSceneGlobalsSchemaTokens, HD_API,
    HDSCENEGLOBALS_SCHEMA_TOKENS);

//-----------------------------------------------------------------------------

/// The HdSceneGlobalsSchema encapsulates "global" state to orchestrate a
/// render. It currently houses the active render settings prim path that
/// describes the information necessary to generate images from a single
/// invocation of a renderer.
///
/// We shall use the convention of a container data source at the root prim
/// of the scene index that is populated with this global state.
/// The renderer and downstream scene indices can query it to configure their
/// behavior as necessary.
///

class HdSceneGlobalsSchema : public HdSchema
{
public:
    HdSceneGlobalsSchema(HdContainerDataSourceHandle container)
    : HdSchema(container) {}

    //ACCESSORS

    HD_API
    HdPathDataSourceHandle GetActiveRenderSettingsPrim();

    // RETRIEVING AND CONSTRUCTING

    /// Builds a container data source which includes the provided child data
    /// sources. Parameters with nullptr values are excluded. This is a
    /// low-level interface. For cases in which it's desired to define
    /// the container with a sparse set of child fields, the Builder class
    /// is often more convenient and readable.
    HD_API
    static HdContainerDataSourceHandle
    BuildRetained(
        const HdPathDataSourceHandle &activeRenderSettingsPrim
    );

    /// \class HdSceneGlobalsSchema::Builder
    /// 
    /// Utility class for setting sparse sets of child data source fields to be
    /// filled as arguments into BuildRetained. Because all setter methods
    /// return a reference to the instance, this can be used in the "builder
    /// pattern" form.
    class Builder
    {
    public:
        HD_API
        Builder &SetActiveRenderSettingsPrim(
            const HdPathDataSourceHandle &activeRenderSettingsPrim);

        /// Returns a container data source containing the members set thus far.
        HD_API
        HdContainerDataSourceHandle Build();

    private:
        HdPathDataSourceHandle _activeRenderSettingsPrim;
    };
    
    /// Constructs and returns a HdSceneGlobalsSchema from the root prim in the
    /// scene index. Since the root prim might not have a data source for this
    /// schema, the result should be checked with IsDefined() or a bool 
    /// conversion before use.
    ///
    /// \note This API is preferable to GetFromParent(container).
    HD_API
    static HdSceneGlobalsSchema
    GetFromSceneIndex(
        const HdSceneIndexBaseRefPtr &si);
    
    /// Utility method to concretize the convention of parking the 
    /// "sceneGlobals" container at the root prim of the scene index.
    static const SdfPath&
    GetDefaultPrimPath() {
        return SdfPath::AbsoluteRootPath();
    }


    /// Retrieves a container data source with the schema's default name token
    /// "sceneGlobals" from the parent container and constructs a
    /// HdSceneGlobalsSchema instance.
    /// Because the requested container data source may not exist, the result
    /// should be checked with IsDefined() or a bool comparison before use.
    HD_API
    static HdSceneGlobalsSchema GetFromParent(
        const HdContainerDataSourceHandle &fromParentContainer);

    /// Returns an HdDataSourceLocator (relative to the prim-level data source)
    /// where the container representing this schema is found by default.
    HD_API
    static const HdDataSourceLocator &GetDefaultLocator();


    /// Returns an HdDataSourceLocator (relative to the prim-level data source)
    /// where the activerendersettingsprim data source can be found.
    /// This is often useful for checking intersection against the
    /// HdDataSourceLocatorSet sent with HdDataSourceObserver::PrimsDirtied.
    HD_API
    static const HdDataSourceLocator &GetActiveRenderSettingsPrimLocator();

};

PXR_NAMESPACE_CLOSE_SCOPE

#endif