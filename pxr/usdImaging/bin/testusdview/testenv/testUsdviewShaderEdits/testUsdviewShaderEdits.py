#!/pxrpythonsubst
#
# Copyright 2019 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.
#
from pxr import UsdShade
from pxr import Sdf
from pxr.Usdviewq.qt import QtWidgets

# Remove any unwanted visuals from the view, and enable autoClip
def _modifySettings(appController):
    appController._dataModel.viewSettings.showBBoxes = False
    appController._dataModel.viewSettings.showHUD = False
    appController._dataModel.viewSettings.autoComputeClippingPlanes = True

def _bugStep1(s):
    # Make a Material that contains a UsdPreviewSurface
    material = UsdShade.Material.Define(s, '/Scene/Looks/NewMaterial')

    # Create the surface.
    pbrShader = UsdShade.Shader.Define(s, '/Scene/Looks/NewMaterial/PbrPreview')
    pbrShader.CreateIdAttr("UsdPreviewSurface")
    pbrShader.CreateInput("roughness", Sdf.ValueTypeNames.Float).Set(0.0)
    pbrShader.CreateInput("metallic", Sdf.ValueTypeNames.Float).Set(0.0)
    pbrShader.CreateInput("diffuseColor", Sdf.ValueTypeNames.Color3f).Set((0.0, 0.0, 1.0))
    material.CreateSurfaceOutput().ConnectToSource(pbrShader.ConnectableAPI(),
            "surface")

    # Now bind the Material to the card
    mesh = s.GetPrimAtPath('/Scene/Geom/Plane')
    UsdShade.MaterialBindingAPI.Apply(mesh).Bind(material)

def _bugStep2(s):
    # create texture coordinate reader 
    stReader = UsdShade.Shader.Define(s, '/Scene/Looks/NewMaterial/STReader')
    stReader.CreateIdAttr('UsdPrimvarReader_float2')
    stReader.CreateInput('varname', Sdf.ValueTypeNames.Token).Set("st")

    # diffuse texture
    diffuseTextureSampler = UsdShade.Shader.Define(s,'/Scene/Looks/NewMaterial/Texture')
    diffuseTextureSampler.CreateIdAttr('UsdUVTexture')
    diffuseTextureSampler.CreateInput('file', Sdf.ValueTypeNames.Asset).Set("test.png")
    diffuseTextureSampler.CreateInput("st", Sdf.ValueTypeNames.Float2).ConnectToSource(stReader.ConnectableAPI(), 'result')
    diffuseTextureSampler.CreateOutput('rgb', Sdf.ValueTypeNames.Float3)

    pbrShader = UsdShade.ConnectableAPI(s.GetPrimAtPath('/Scene/Looks/NewMaterial/PbrPreview'))
    pbrShader.CreateInput("diffuseColor", Sdf.ValueTypeNames.Color3f).ConnectToSource(diffuseTextureSampler.ConnectableAPI(), 'rgb')

def _bugStep3(s):
    # change diffuse texture
    diffuseTextureSampler = UsdShade.ConnectableAPI(s.GetPrimAtPath('/Scene/Looks/NewMaterial/Texture'))
    diffuseTextureSampler.CreateInput('file', Sdf.ValueTypeNames.Asset).Set("test2.png")

# Test material bindings edits.
def testUsdviewInputFunction(appController):
    _modifySettings(appController)
    s = appController._dataModel.stage
    l = s.GetSessionLayer()
    s.SetEditTarget(l)

    # Turn off the built-in camera light
    appController._ambientOnlyClicked(False)

    _bugStep1(s)
    # Wait for usdview to catch up with changes, and since we are not interested
    # in the final image at this point, we are fine not waiting for convergence
    appController._processEvents()

    _bugStep2(s)
    # Wait for usdview to catch up with changes, and since we are not interested
    # in the final image at this point, we are fine not waiting for convergence
    appController._processEvents()

    _bugStep3(s)
    # Wait for usdview to catch up with changes, and since we are not interested
    # in the final image at this point, we are fine not waiting for convergence
    appController._processEvents()

    # wait for renderer to converge - before capturing the shot
    appController._takeShot("0.png", waitForConvergence=True)
