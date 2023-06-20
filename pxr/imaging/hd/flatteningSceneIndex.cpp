//
// Copyright 2021 Pixar
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
#include "pxr/imaging/hd/flatteningSceneIndex.h"
#include "pxr/imaging/hd/overlayContainerDataSource.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/xformSchema.h"
#include "pxr/imaging/hd/purposeSchema.h"
#include "pxr/imaging/hd/visibilitySchema.h"
#include "pxr/imaging/hd/materialBindingsSchema.h"
#include "pxr/imaging/hd/primvarsSchema.h"
#include "pxr/imaging/hd/coordSysBindingSchema.h"
#include "pxr/base/trace/trace.h"
#include "pxr/base/work/utils.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (model)
    (drawMode)
    (inherited)
    (strongerThanDescendants)
);

namespace {

const HdDataSourceLocator &_GetDrawModeLocator()
{
    static const HdDataSourceLocator l(_tokens->model, _tokens->drawMode);
    return l;
}

// Defaults to true if no data source given.
bool _GetBoolValue(const HdContainerDataSourceHandle &ds,
                   const TfToken &name)
{
    if (!ds) {
        return true;
    }
    HdBoolDataSourceHandle const bds =
        HdBoolDataSource::Cast(
            ds->Get(name));
    if (!bds) {
        return false;
    }
    return bds->GetTypedValue(0.0f);
}


// Parent and local bindings might have unique fields so we must
// overlay them. If we are concerned about overlay depth, we could
// compare GetNames() results to decide whether the child bindings
// completely mask the parent.
//
// Like an HdOverlayContainerDataSource, but looking at bindingStrength
// to determine which data source is stronger.
//
class _MaterialBindingsDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(_MaterialBindingsDataSource);

    TfTokenVector GetNames() override {
        TfDenseHashSet<TfToken, TfToken::HashFunctor> allNames;
        {
            for (const TfTokenVector names : { _primBindings->GetNames(),
                                               _parentBindings->GetNames() } ) {
                allNames.insert(names.begin(), names.end());
            }
        }

        return { allNames.begin(), allNames.end() };
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override {
        HdMaterialBindingSchema parentSchema(
            HdContainerDataSource::Cast(
                _parentBindings->Get(name)));
        if (HdTokenDataSourceHandle const strengthDs =
                parentSchema.GetBindingStrength()) {
            const TfToken strength = strengthDs->GetTypedValue(0.0f);
            if (strength == _tokens->strongerThanDescendants) {
                return parentSchema.GetContainer();
            }
        }
        if (HdDataSourceBaseHandle const bindingDs = _primBindings->Get(name)) {
            return bindingDs;
        }
        return parentSchema.GetContainer();
    }

    // Return data source with the correct composition behavior.
    //
    // This avoids allocating the _MaterialBindingsDataSource if only one
    // of the given handles is non-null.
    static
    HdContainerDataSourceHandle
    UseOrCreateNew(
        const HdContainerDataSourceHandle &primBindings,
        const HdContainerDataSourceHandle &parentBindings)
    {
        if (!primBindings) {
            return parentBindings;
        }
        if (!parentBindings) {
            return primBindings;
        }
        return New(primBindings, parentBindings);
    }

private:
    _MaterialBindingsDataSource(
        const HdContainerDataSourceHandle &primBindings,
        const HdContainerDataSourceHandle &parentBindings)
      : _primBindings(primBindings)
      , _parentBindings(parentBindings)
    {
    }

    HdContainerDataSourceHandle const _primBindings;
    HdContainerDataSourceHandle const _parentBindings;
};

}

HdFlatteningSceneIndex::HdFlatteningSceneIndex(
        HdSceneIndexBaseRefPtr const &inputScene,
        HdContainerDataSourceHandle const &inputArgs)
    : HdSingleInputFilteringSceneIndexBase(inputScene)

    , _flattenXform(
        _GetBoolValue(inputArgs,
                      HdXformSchemaTokens->xform))
    , _flattenVisibility(
        _GetBoolValue(inputArgs,
                      HdVisibilitySchemaTokens->visibility))
    , _flattenPurpose(
        _GetBoolValue(inputArgs,
                      HdPurposeSchemaTokens->purpose))
    , _flattenModel(
        _GetBoolValue(inputArgs,
                      _tokens->model))
    , _flattenMaterialBindings(
        _GetBoolValue(inputArgs,
                      HdMaterialBindingsSchema::GetSchemaToken()))
    , _flattenPrimvars(
        _GetBoolValue(inputArgs,
                      HdPrimvarsSchemaTokens->primvars))
    , _flattenCoordSysBinding(
        _GetBoolValue(inputArgs,
                    HdCoordSysBindingSchemaTokens->coordSysBinding))
    , _identityXform(HdXformSchema::Builder()
        .SetMatrix(
            HdRetainedTypedSampledDataSource<GfMatrix4d>::New(
                    GfMatrix4d().SetIdentity()))
        .Build())

    , _identityVis(HdVisibilitySchema::Builder()
        .SetVisibility(
            HdRetainedTypedSampledDataSource<bool>::New(true))
        .Build())

    , _identityPurpose(HdPurposeSchema::Builder()
        .SetPurpose(
            HdRetainedTypedSampledDataSource<TfToken>::New(
                HdRenderTagTokens->geometry))
        .Build())

    , _identityDrawMode(
        HdRetainedTypedSampledDataSource<TfToken>::New(TfToken()))
{
    if (_flattenXform) {
        _dataSourceNames.push_back(
            HdXformSchemaTokens->xform);
    }
    if (_flattenVisibility) {
        _dataSourceNames.push_back(
            HdVisibilitySchemaTokens->visibility);
    }
    if (_flattenPurpose) {
        _dataSourceNames.push_back(
            HdPurposeSchemaTokens->purpose);
    }
    if (_flattenModel) {
        _dataSourceNames.push_back(
            _tokens->model);
    }
    if (_flattenMaterialBindings) {
        _dataSourceNames.push_back(
            HdMaterialBindingsSchema::GetSchemaToken());
    }
    if (_flattenPrimvars) {
        _dataSourceNames.push_back(
            HdPrimvarsSchemaTokens->primvars);
    }
    if (_flattenCoordSysBinding) {
        _dataSourceNames.push_back(
                HdCoordSysBindingSchemaTokens->coordSysBinding);
    }
}

HdFlatteningSceneIndex::~HdFlatteningSceneIndex() = default;

HdSceneIndexPrim
HdFlatteningSceneIndex::GetPrim(const SdfPath &primPath) const
{
    // Check the hierarchy cache
    {
        const _PrimTable::const_iterator i = _prims.find(primPath);
        // SdfPathTable will default-construct entries for ancestors
        // as needed to represent hierarchy, so double-check the
        // dataSource to confirm presence of a cached prim
        if (i != _prims.end() && i->second.dataSource) {
            return i->second;
        }
    }

    // Check the recent prims cache
    {
        // Use a scope to minimize lifetime of tbb accessor
        // for maximum concurrency
        _RecentPrimTable::const_accessor accessor;
        if (_recentPrims.find(accessor, primPath)) {
            return accessor->second;
        }
    }

    // No cache entry found; query input scene
    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);

    // Wrap the input dataSource even when null, to support
    // dirtying down the hierarchy
    prim.dataSource = _PrimLevelWrappingDataSource::New(
        *this, primPath, prim.dataSource);

    // Store in the recent prims cache
    if (!_recentPrims.insert(std::make_pair(primPath, prim))) {
        // Another thread inserted this entry.  Since dataSources
        // are stateful, return that one.
        _RecentPrimTable::accessor accessor;
        if (TF_VERIFY(_recentPrims.find(accessor, primPath))) {
            prim = accessor->second;
        }
    }
    return prim;
}

SdfPathVector
HdFlatteningSceneIndex::GetChildPrimPaths(const SdfPath &primPath) const
{
    // we don't change topology so we can dispatch to input
    return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
}

void
HdFlatteningSceneIndex::_PrimsAdded(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::AddedPrimEntries &entries)
{
    TRACE_FUNCTION();

    _ConsolidateRecentPrims();

    // Check the hierarchy for cached prims to dirty
    HdSceneIndexObserver::DirtiedPrimEntries dirtyEntries;
    for (const HdSceneIndexObserver::AddedPrimEntry &entry : entries) {
        static const HdDataSourceLocatorSet locators = {
            HdXformSchema::GetDefaultLocator(),
            HdVisibilitySchema::GetDefaultLocator(),
            HdPurposeSchema::GetDefaultLocator(),
            _GetDrawModeLocator(),
            HdMaterialBindingsSchema::GetDefaultLocator(),
            HdPrimvarsSchema::GetDefaultLocator()
        };
        _DirtyHierarchy(entry.primPath, locators, &dirtyEntries);
    }

    // Clear out any cached dataSources for prims that have been re-added.
    // They will get updated dataSources in the next call to GetPrim().
    for (const HdSceneIndexObserver::AddedPrimEntry &entry : entries) {
        const _PrimTable::iterator i = _prims.find(entry.primPath);
        if (i != _prims.end()) {
            WorkSwapDestroyAsync(i->second.dataSource);
        }
    }

    _SendPrimsAdded(entries);
    if (!dirtyEntries.empty()) {
        _SendPrimsDirtied(dirtyEntries);
    }
}

void
HdFlatteningSceneIndex::_PrimsRemoved(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::RemovedPrimEntries &entries)
{
    TRACE_FUNCTION();

    _ConsolidateRecentPrims();

    for (const HdSceneIndexObserver::RemovedPrimEntry &entry : entries) {
        if (entry.primPath.IsAbsoluteRootPath()) {
            // Special case removing the whole scene, since this is a common
            // shutdown operation.
            _prims.ClearInParallel();
            TfReset(_prims);
        } else {
            auto startEndIt = _prims.FindSubtreeRange(entry.primPath);
            for (auto it = startEndIt.first; it != startEndIt.second; ++it) {
                WorkSwapDestroyAsync(it->second.dataSource);
            }
            if (startEndIt.first != startEndIt.second) {
                _prims.erase(startEndIt.first);
            }
        }
    }
    _SendPrimsRemoved(entries);
}

void
HdFlatteningSceneIndex::_PrimsDirtied(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::DirtiedPrimEntries &entries)
{
    TRACE_FUNCTION();

    _ConsolidateRecentPrims();

    HdSceneIndexObserver::DirtiedPrimEntries dirtyEntries;

    for (const HdSceneIndexObserver::DirtiedPrimEntry &entry : entries) {
        HdDataSourceLocatorSet locators;
        if (entry.dirtyLocators.Intersects(
                    HdXformSchema::GetDefaultLocator())) {
            locators.insert(HdXformSchema::GetDefaultLocator());
        }
        if (entry.dirtyLocators.Intersects(
                    HdVisibilitySchema::GetDefaultLocator())) {
            locators.insert(HdVisibilitySchema::GetDefaultLocator());
        }
        if (entry.dirtyLocators.Intersects(
                    HdPurposeSchema::GetDefaultLocator())) {
            locators.insert(HdPurposeSchema::GetDefaultLocator());
        }
        if (entry.dirtyLocators.Intersects(_GetDrawModeLocator())) {
            locators.insert(_GetDrawModeLocator());
        }
        if (entry.dirtyLocators.Intersects(
                HdMaterialBindingsSchema::GetDefaultLocator())) {
            locators.insert(HdMaterialBindingsSchema::GetDefaultLocator());
        }
        locators.insert(
            HdFlattenedPrimvarsDataSource::ComputeDirtyPrimvarsLocators(
                entry.dirtyLocators));
        if (entry.dirtyLocators.Intersects(
                HdCoordSysBindingSchema::GetDefaultLocator())) {
            locators.insert(HdCoordSysBindingSchema::GetDefaultLocator());
        }
        
        if (!locators.IsEmpty()) {
            _DirtyHierarchy(entry.primPath, locators, &dirtyEntries);
        }

        // Empty locator indicates that we need to pull the input data source
        // again - which we achieve by destroying the data source wrapping the
        // input data source.
        // Note that we destroy it after calling _DirtyHierarchy to not prevent
        // _DirtyHierarchy propagating the invalidation to the ancestors.
        if (entry.dirtyLocators.Contains(HdDataSourceLocator::EmptyLocator())) {
            const _PrimTable::iterator it = _prims.find(entry.primPath);
            if (it != _prims.end() && it->second.dataSource) {
                WorkSwapDestroyAsync(it->second.dataSource);
            }
        }
    }

    _SendPrimsDirtied(entries);
    if (!dirtyEntries.empty()) {
        _SendPrimsDirtied(dirtyEntries);
    }
}

void
HdFlatteningSceneIndex::_ConsolidateRecentPrims()
{
    for (auto &entry: _recentPrims) {
        std::swap(_prims[entry.first], entry.second);
    }
    _recentPrims.clear();
}

void
HdFlatteningSceneIndex::_DirtyHierarchy(
    const SdfPath &primPath,
    const HdDataSourceLocatorSet &dirtyLocators,
    HdSceneIndexObserver::DirtiedPrimEntries *dirtyEntries)
{
    // XXX: here and elsewhere, if a parent xform is dirtied and the child has
    // resetXformStack, we could skip dirtying the child...

    auto startEndIt = _prims.FindSubtreeRange(primPath);
    auto it = startEndIt.first;
    for (; it != startEndIt.second; ) {
        HdSceneIndexPrim &prim = it->second;

        if (_PrimLevelWrappingDataSourceHandle dataSource =
                _PrimLevelWrappingDataSource::Cast(prim.dataSource)) {
            if (dataSource->PrimDirtied(dirtyLocators)) {
                // If we invalidated any data for any prim besides "primPath"
                // (which already has a notice), generate a new PrimsDirtied
                // notice.
                if (it->first != primPath) {
                    dirtyEntries->emplace_back(it->first, dirtyLocators);
                }
                ++it;
            } else {
                // If we didn't invalidate any data, we can safely assume that
                // no downstream prims depended on this prim for their
                // flattened result, and skip to the next subtree. This is
                // an important optimization for (e.g.) scene population,
                // where no data is cached yet...
                it = it.GetNextSubtree();
            }
        } else {
            ++it;
        }
    }
}

HdFlatteningSceneIndex::
_PrimLevelWrappingDataSource::_PrimLevelWrappingDataSource(
        const HdFlatteningSceneIndex &scene,
        const SdfPath &primPath,
        HdContainerDataSourceHandle inputDataSource)
    : _sceneIndex(scene)
    , _primPath(primPath)
    , _inputDataSource(inputDataSource)
{
}

bool
HdFlatteningSceneIndex::_PrimLevelWrappingDataSource::PrimDirtied(
        const HdDataSourceLocatorSet &set)
{
    static const HdContainerDataSourceHandle containerNull;
    static const HdTokenDataSourceHandle tokenNull;
    static const HdDataSourceBaseHandle baseNull;
    static const HdFlattenedPrimvarsDataSourceHandle flattenedPrimvarsNull;

    bool anyDirtied = false;

    if (set.Intersects(HdXformSchema::GetDefaultLocator())) {
        if (HdContainerDataSource::AtomicLoad(_computedXformDataSource)) {
            anyDirtied = true;
        }
        HdContainerDataSource::AtomicStore(
            _computedXformDataSource, containerNull);
    }
    if (set.Intersects(HdVisibilitySchema::GetDefaultLocator())) {
        if (HdContainerDataSource::AtomicLoad(_computedVisDataSource)) {
            anyDirtied = true;
        }
        HdContainerDataSource::AtomicStore(
            _computedVisDataSource, containerNull);
    }
    if (set.Intersects(HdPurposeSchema::GetDefaultLocator())) {
        if (HdContainerDataSource::AtomicLoad(_computedPurposeDataSource)) {
            anyDirtied = true;
        }
        HdContainerDataSource::AtomicStore(
            _computedPurposeDataSource, containerNull);
    }
    if (set.Intersects(_GetDrawModeLocator())) {
        if (HdTokenDataSource::AtomicLoad(_computedDrawModeDataSource)) {
            anyDirtied = true;
        }
        HdTokenDataSource::AtomicStore(
            _computedDrawModeDataSource, tokenNull);
    }
    if (set.Intersects(HdMaterialBindingsSchema::GetDefaultLocator())) {
        if (HdDataSourceBase::AtomicLoad(_computedMaterialBindingsDataSource)) {
            anyDirtied = true;
        }
        HdDataSourceBase::AtomicStore(
            _computedMaterialBindingsDataSource, baseNull);
    }
    if (set.Intersects(HdPrimvarsSchema::GetDefaultLocator())) {
        if (set.Contains(HdPrimvarsSchema::GetDefaultLocator())) {
            // If HdFlattenedPrimvarsDataSource::ComputeDirtyPrimvarsLocators
            // returned just the "primvars" data source locator, drop the entire
            // flattened primvars data source.
            if (HdFlattenedPrimvarsDataSource::AtomicLoad(
                    _computedPrimvarsDataSource)) {
                anyDirtied = true;
            }
            HdFlattenedPrimvarsDataSource::AtomicStore(
                _computedPrimvarsDataSource, flattenedPrimvarsNull);
        } else {
            // Otherwise, we can just invalidate the primvars in question.
            if (_computedPrimvarsDataSource) {
                if (_computedPrimvarsDataSource->Invalidate(set)) {
                    anyDirtied = true;
                }
            }
        }
    }
    if (set.Intersects(HdCoordSysBindingSchema::GetDefaultLocator())) {
        if (HdDataSourceBase::AtomicLoad(_computedCoordSysBindingDataSource)) {
            anyDirtied = true;
        }
        HdDataSourceBase::AtomicStore(
            _computedCoordSysBindingDataSource, baseNull);
    }

    return anyDirtied;
}

static
void
_Insert(const TfTokenVector &vec,
        TfTokenVector * const result)
{
    if (vec.size() > 31) {
        std::unordered_set<TfToken, TfHash> s(vec.begin(), vec.end());
        for (const TfToken &t : *result) {
            s.erase(t);
        }
        for (const TfToken &t : vec) {
            if (s.find(t) != s.end()) {
                result->push_back(t);
            }
        }
    } else {
        uint32_t mask = (1 << vec.size()) - 1;
        for (const TfToken &t : *result) {
            for (size_t i = 0; i < vec.size(); i++) {
                if (vec[i] == t) {
                    mask &= ~(1 << i);
                }
            }
            if (!mask) {
                return;
            }
        }
        for (size_t i = 0; i < vec.size(); i++) {
            if (mask & 1 << i) {
                result->push_back(vec[i]);
            }
        }
    }
}

TfTokenVector
HdFlatteningSceneIndex::_PrimLevelWrappingDataSource::GetNames()
{
    if (!_inputDataSource) {
        return _sceneIndex._dataSourceNames;
    }

    TfTokenVector result = _inputDataSource->GetNames();
    _Insert(_sceneIndex._dataSourceNames, &result);
    return result;
};        

HdDataSourceBaseHandle
HdFlatteningSceneIndex::_PrimLevelWrappingDataSource::Get(
        const TfToken &name)
{
    if (_sceneIndex._flattenXform &&
        name == HdXformSchemaTokens->xform) {
        return _GetXform();
    }
    if (_sceneIndex._flattenVisibility &&
        name == HdVisibilitySchemaTokens->visibility) {
        return _GetVis();
    }
    if (_sceneIndex._flattenPurpose &&
        name == HdPurposeSchemaTokens->purpose) {
        return _GetPurpose();
    }
    if (_sceneIndex._flattenModel &&
        name == _tokens->model) {
        return _GetModel();
    }
    if (_sceneIndex._flattenMaterialBindings &&
        name == HdMaterialBindingsSchema::GetSchemaToken()) {
        return _GetMaterialBindings();
    }
    if (_sceneIndex._flattenPrimvars &&
        name == HdPrimvarsSchemaTokens->primvars) {
        return _GetPrimvars();
    }
    if (_sceneIndex._flattenCoordSysBinding &&
        name == HdCoordSysBindingSchemaTokens->coordSysBinding) {
        return _GetCoordSysBinding();
    }
    if (_inputDataSource) {
        return _inputDataSource->Get(name);
    }
    return nullptr;
}

HdContainerDataSourceHandle
HdFlatteningSceneIndex::_PrimLevelWrappingDataSource::
_GetParentPrimDataSource() const
{
    if (_primPath.IsAbsoluteRootPath()) {
        return nullptr;
    }
    return _sceneIndex.GetPrim(_primPath.GetParentPath()).dataSource;
}

HdDataSourceBaseHandle
HdFlatteningSceneIndex::_PrimLevelWrappingDataSource::_GetPurpose()
{
    HdContainerDataSourceHandle computedPurposeDataSource =
        HdContainerDataSource::AtomicLoad(_computedPurposeDataSource);

    if (computedPurposeDataSource) {
        return computedPurposeDataSource;
    }

    HdPurposeSchema inputPurpose =
        HdPurposeSchema::GetFromParent(_inputDataSource);

    if (inputPurpose && inputPurpose.GetPurpose()) {
        computedPurposeDataSource = inputPurpose.GetContainer();
    } else {
        HdPurposeSchema parentPurpose(nullptr);
        if (_primPath.GetPathElementCount()) {
            parentPurpose =
                HdPurposeSchema::GetFromParent(_GetParentPrimDataSource());
        }
        if (parentPurpose && parentPurpose.GetPurpose()) {
            computedPurposeDataSource = parentPurpose.GetContainer();
        } else {
            computedPurposeDataSource = _sceneIndex._identityPurpose;
        }
    }

    HdContainerDataSource::AtomicStore(
            _computedPurposeDataSource, computedPurposeDataSource);

    return computedPurposeDataSource;
}

HdDataSourceBaseHandle
HdFlatteningSceneIndex::_PrimLevelWrappingDataSource::_GetVis()
{
    HdContainerDataSourceHandle computedVisDataSource =
        HdContainerDataSource::AtomicLoad(_computedVisDataSource);

    if (computedVisDataSource) {
        return computedVisDataSource;
    }

    HdVisibilitySchema inputVis =
        HdVisibilitySchema::GetFromParent(_inputDataSource);

    if (inputVis && inputVis.GetVisibility()) {
        computedVisDataSource = inputVis.GetContainer();
    } else {
        HdVisibilitySchema parentVis(nullptr);
        if (_primPath.GetPathElementCount()) {
            parentVis =
                HdVisibilitySchema::GetFromParent(_GetParentPrimDataSource());
        }
        if (parentVis && parentVis.GetVisibility()) {
            computedVisDataSource = parentVis.GetContainer();
        } else {
            computedVisDataSource = _sceneIndex._identityVis;
        }
    }

    HdContainerDataSource::AtomicStore(
            _computedVisDataSource, computedVisDataSource);

    return computedVisDataSource;
}

HdDataSourceBaseHandle
HdFlatteningSceneIndex::_PrimLevelWrappingDataSource::_GetXform()
{
    HdContainerDataSourceHandle computedXformDataSource =
            HdContainerDataSource::AtomicLoad(_computedXformDataSource);

    // previously cached value
    if (computedXformDataSource) {
        return computedXformDataSource;
    }

    HdXformSchema inputXform =
            HdXformSchema::GetFromParent(_inputDataSource);

    // If this xform is fully composed, early out.
    if (inputXform) {
        HdBoolDataSourceHandle resetXformStack =
            inputXform.GetResetXformStack();
        if (resetXformStack && resetXformStack->GetTypedValue(0.0f)) {
            // Only use the local transform, or identity if no matrix was
            // provided...
            if (inputXform.GetMatrix()) {
                computedXformDataSource = inputXform.GetContainer();
            } else {
                computedXformDataSource = _sceneIndex._identityXform;
            }
            HdContainerDataSource::AtomicStore(
                _computedXformDataSource, computedXformDataSource);

            return computedXformDataSource;
        }
    }

    // Otherwise, we need to look at the parent value.
    HdXformSchema parentXform(nullptr);
    if (_primPath.GetPathElementCount()) {
        parentXform =
            HdXformSchema::GetFromParent(_GetParentPrimDataSource());
    }

    // Attempt to compose the local matrix with the parent matrix;
    // note that since we got the parent matrix from GetPrim() instead of
    // _inputDataSource, the parent matrix should be flattened already.
    // If either of the local or parent matrix are missing, they are
    // interpreted to be identity.
    HdMatrixDataSourceHandle parentMatrixDataSource =
        parentXform ? parentXform.GetMatrix() : nullptr;
    HdMatrixDataSourceHandle inputMatrixDataSource =
        inputXform ? inputXform.GetMatrix() : nullptr;

    if (inputMatrixDataSource && parentMatrixDataSource) {
        GfMatrix4d parentMatrix =
            parentMatrixDataSource->GetTypedValue(0.0f);
        GfMatrix4d inputMatrix =
            inputMatrixDataSource->GetTypedValue(0.0f);

        computedXformDataSource = HdXformSchema::Builder()
            .SetMatrix(
                    HdRetainedTypedSampledDataSource<GfMatrix4d>::New(
                        inputMatrix * parentMatrix))
            .Build();
    } else if (inputMatrixDataSource) {
        computedXformDataSource = inputXform.GetContainer();
    } else if (parentMatrixDataSource) {
        computedXformDataSource = parentXform.GetContainer();
    } else {
        computedXformDataSource = _sceneIndex._identityXform;
    }

    HdContainerDataSource::AtomicStore(
            _computedXformDataSource, computedXformDataSource);

    return computedXformDataSource;
}

HdDataSourceBaseHandle
HdFlatteningSceneIndex::_PrimLevelWrappingDataSource::_GetModel()
{
    HdContainerDataSourceHandle const modelContainer =
        _inputDataSource
            ? HdContainerDataSource::Cast(_inputDataSource->Get(_tokens->model))
            : nullptr;
    HdContainerDataSourceHandle const overrideContainer =
        HdRetainedContainerDataSource::New(
            _tokens->drawMode, _GetDrawMode(modelContainer));
    if (!modelContainer) {
        return overrideContainer;
    }
    return HdOverlayContainerDataSource::New(overrideContainer, modelContainer);
}

HdDataSourceBaseHandle
HdFlatteningSceneIndex::_PrimLevelWrappingDataSource::_GetDrawMode(
    const HdContainerDataSourceHandle &modelContainer)
{
    HdTokenDataSource::AtomicHandle computedDrawModeDataSource =
        HdTokenDataSource::AtomicLoad(_computedDrawModeDataSource);

    if (computedDrawModeDataSource) {
        return computedDrawModeDataSource;
    }

    computedDrawModeDataSource = _GetDrawModeUncached(modelContainer);

    HdTokenDataSource::AtomicStore(
        _computedDrawModeDataSource, computedDrawModeDataSource);

    return computedDrawModeDataSource;
}

HdTokenDataSourceHandle
HdFlatteningSceneIndex::_PrimLevelWrappingDataSource::_GetDrawModeUncached(
    const HdContainerDataSourceHandle &modelContainer)
{
    if (modelContainer) {
        if (const HdTokenDataSourceHandle src =
                HdTokenDataSource::Cast(
                    modelContainer->Get(_tokens->drawMode))) {
            const TfToken drawMode = src->GetTypedValue(0.0f);
            if (!drawMode.IsEmpty() && drawMode != _tokens->inherited) {
                return src;
            }
        }
    }

    if (_primPath.GetPathElementCount() == 0) {
        return _sceneIndex._identityDrawMode;
    }

    if (const HdTokenDataSourceHandle src =
        HdTokenDataSource::Cast(
            HdContainerDataSource::Get(
                _GetParentPrimDataSource(),
                _GetDrawModeLocator()))) {
        return src;
    }

    return _sceneIndex._identityDrawMode;
}

HdDataSourceBaseHandle
HdFlatteningSceneIndex::_PrimLevelWrappingDataSource::_GetMaterialBindings()
{
    HdDataSourceBaseHandle result =
        HdDataSourceBase::AtomicLoad(_computedMaterialBindingsDataSource);

    if (!result) {
        result = _GetMaterialBindingsUncached();
        if (!result) {
            // Cache the absence of value by storing a non-container which will
            // fail the cast on return. Using retained "false" because its New
            // returns a shared instance rather than a new allocation.
            result = HdRetainedTypedSampledDataSource<bool>::New(false);
        }
        HdDataSourceBase::AtomicStore(
            _computedMaterialBindingsDataSource, result);
    }

    // The cached value of the absence of a materialBinding is a non-container
    // data source.
    return HdContainerDataSource::Cast(result);
}

HdContainerDataSourceHandle
HdFlatteningSceneIndex::_PrimLevelWrappingDataSource::
_GetMaterialBindingsUncached()
{
    return
        _MaterialBindingsDataSource::UseOrCreateNew(
            HdMaterialBindingsSchema::GetFromParent(_inputDataSource)
                .GetContainer(),
            HdMaterialBindingsSchema::GetFromParent(_GetParentPrimDataSource())
                .GetContainer());
}

HdFlattenedPrimvarsDataSourceHandle
HdFlatteningSceneIndex::_PrimLevelWrappingDataSource::_GetPrimvars()
{
    HdFlattenedPrimvarsDataSourceHandle result =
        HdFlattenedPrimvarsDataSource::AtomicLoad(
            _computedPrimvarsDataSource);

    if (!result) {
        result = _GetPrimvarsUncached();
        HdFlattenedPrimvarsDataSource::AtomicStore(
            _computedPrimvarsDataSource,
            result);
    }

    return result;
}

HdFlattenedPrimvarsDataSourceHandle
HdFlatteningSceneIndex::_PrimLevelWrappingDataSource::
_GetPrimvarsUncached()
{
    HdContainerDataSourceHandle inputPrimvars =
        HdPrimvarsSchema::GetFromParent(_inputDataSource).GetContainer();

    HdFlattenedPrimvarsDataSourceHandle parentPrimvars;
    if (_primPath.GetPathElementCount()) {
        parentPrimvars =
            HdFlattenedPrimvarsDataSource::Cast(
                HdPrimvarsSchema::GetFromParent(
                    _GetParentPrimDataSource()).GetContainer());
    }

    return HdFlattenedPrimvarsDataSource::New(
        inputPrimvars, parentPrimvars);
}

HdDataSourceBaseHandle
HdFlatteningSceneIndex::_PrimLevelWrappingDataSource::_GetCoordSysBinding()
{
    HdDataSourceBaseHandle result =
        HdDataSourceBase::AtomicLoad(_computedCoordSysBindingDataSource);

    if (!result) {
        result = _GetCoordSysBindingUncached();
        if (!result) {
            // Cache the absence of value by storing a non-container which will
            // fail the cast on return. Using retained "false" because its New
            // returns a shared instance rather than a new allocation.
            result = HdRetainedTypedSampledDataSource<bool>::New(false);
        }
        HdDataSourceBase::AtomicStore(
            _computedCoordSysBindingDataSource, result);
    }

    return HdContainerDataSource::Cast(result);
}

HdContainerDataSourceHandle
HdFlatteningSceneIndex::_PrimLevelWrappingDataSource::
_GetCoordSysBindingUncached()
{
    HdContainerDataSourceHandle inputBindings =
        HdCoordSysBindingSchema::GetFromParent(_inputDataSource)
            .GetContainer();
    HdContainerDataSourceHandle parentBindings =
        HdCoordSysBindingSchema::GetFromParent(_GetParentPrimDataSource())
            .GetContainer();
        
    if (!inputBindings) {
        return parentBindings;
    }
    if (!parentBindings) {
        return inputBindings;
    }

    // Parent and local bindings might have unique fields so we must
    // overlay them. If we are concerned about overlay depth, we could
    // compare GetNames() results to decide whether the child bindings
    // completely mask the parent.
    return HdOverlayContainerDataSource::New(inputBindings, parentBindings);
}

PXR_NAMESPACE_CLOSE_SCOPE
