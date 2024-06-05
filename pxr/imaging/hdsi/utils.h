//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_HDSI_UTILS_H
#define PXR_IMAGING_HDSI_UTILS_H

#include "pxr/pxr.h"
#include "pxr/imaging/hdsi/api.h"
#include "pxr/usd/sdf/path.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdCollectionExpressionEvaluator;

/// Prunes the given list of children using the supplied evaluator.
HDSI_API
void
HdsiUtilsRemovePrunedChildren(
    const SdfPath &parentPath,
    const HdCollectionExpressionEvaluator &eval,
    SdfPathVector *children);

PXR_NAMESPACE_CLOSE_SCOPE

#endif //PXR_IMAGING_HDSI_UTILS_H