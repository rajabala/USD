//
// Copyright 2023 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
////////////////////////////////////////////////////////////////////////

/* ************************************************************************** */
/* **                                                                      ** */
/* ** This file is generated by a script.                                  ** */
/* **                                                                      ** */
/* ** Do not edit it directly (unless it is within a CUSTOM CODE section)! ** */
/* ** Edit hdSchemaDefs.py instead to make changes.                        ** */
/* **                                                                      ** */
/* ************************************************************************** */

#include "pxr/imaging/hd/volumeFieldBindingSchema.h"

#include "pxr/imaging/hd/retainedDataSource.h"

#include "pxr/base/trace/trace.h"

// --(BEGIN CUSTOM CODE: Includes)--
// --(END CUSTOM CODE: Includes)--

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(HdVolumeFieldBindingSchemaTokens,
    HD_VOLUME_FIELD_BINDING_SCHEMA_TOKENS);

// --(BEGIN CUSTOM CODE: Schema Methods)--
// --(END CUSTOM CODE: Schema Methods)--

TfTokenVector
HdVolumeFieldBindingSchema::GetVolumeFieldBindingNames() const
{
    if (_container) {
        return _container->GetNames();
    } else {
        return {};
    }
}

HdPathDataSourceHandle
HdVolumeFieldBindingSchema::GetVolumeFieldBinding(const TfToken &name) const
{
    return _GetTypedDataSource<HdPathDataSource>(name);
}

/*static*/
HdContainerDataSourceHandle
HdVolumeFieldBindingSchema::BuildRetained(
    const size_t count,
    const TfToken * const names,
    const HdDataSourceBaseHandle * const values)
{
    return HdRetainedContainerDataSource::New(count, names, values);
}

/*static*/
HdVolumeFieldBindingSchema
HdVolumeFieldBindingSchema::GetFromParent(
        const HdContainerDataSourceHandle &fromParentContainer)
{
    return HdVolumeFieldBindingSchema(
        fromParentContainer
        ? HdContainerDataSource::Cast(fromParentContainer->Get(
                HdVolumeFieldBindingSchemaTokens->volumeFieldBinding))
        : nullptr);
}

/*static*/
const TfToken &
HdVolumeFieldBindingSchema::GetSchemaToken()
{
    return HdVolumeFieldBindingSchemaTokens->volumeFieldBinding;
}

/*static*/
const HdDataSourceLocator &
HdVolumeFieldBindingSchema::GetDefaultLocator()
{
    static const HdDataSourceLocator locator(GetSchemaToken());
    return locator;
} 

PXR_NAMESPACE_CLOSE_SCOPE