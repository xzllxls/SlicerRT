/*==============================================================================

  Copyright (c) Laboratory for Percutaneous Surgery (PerkLab)
  Queen's University, Kingston, ON, Canada. All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  This file was originally developed by Csaba Pinter, PerkLab, Queen's University
  and was supported through the Applied Cancer Research Unit program of Cancer Care
  Ontario with funds provided by the Ontario Ministry of Health and Long-Term Care

==============================================================================*/

// .NAME qSlicerSegmentEditorLabelEffect - Logic class for segmentation handling
// .SECTION Description
// TODO

#ifndef __qSlicerSegmentEditorLabelEffect_h
#define __qSlicerSegmentEditorLabelEffect_h

// Segmentations Widgets includes
#include "qSlicerSegmentationsModuleWidgetsExport.h"

#include "qSlicerSegmentEditorAbstractEffect.h"

class qSlicerSegmentEditorLabelEffectPrivate;

class vtkMatrix4x4;
class vtkOrientedImageData;
class vtkMRMLVolumeNode;
class vtkMRMLSegmentationNode;

/// \ingroup SlicerRt_QtModules_Segmentations
class Q_SLICER_MODULE_SEGMENTATIONS_WIDGETS_EXPORT qSlicerSegmentEditorLabelEffect :
  public qSlicerSegmentEditorAbstractEffect
{
public:
  Q_OBJECT

public:
  typedef qSlicerSegmentEditorAbstractEffect Superclass;
  qSlicerSegmentEditorLabelEffect(QObject* parent = NULL);
  virtual ~qSlicerSegmentEditorLabelEffect(); 

public:  
  /// Get name of effect
  virtual QString name() = 0;

  /// Clone editor effect
  virtual qSlicerSegmentEditorAbstractEffect* clone() = 0;

  /// Update user interface from parameter set node
  virtual void updateGUIFromMRML(vtkObject* caller, void* callData);

// Utility functions
public:
  /// Return matrix for volume node that takes into account the IJKToRAS
  /// and any linear transforms that have been applied
  static void ijkToRasMatrix(vtkMRMLVolumeNode* node, vtkMatrix4x4* ijkToRas);

  /// Return matrix for volume node that takes into account the IJKToRAS
  /// and any linear transforms that have been applied
  static void ijkToRasMatrix(vtkOrientedImageData* image, vtkMRMLSegmentationNode* node, vtkMatrix4x4* ijkToRas);

private:
  Q_DECLARE_PRIVATE(qSlicerSegmentEditorLabelEffect);
  Q_DISABLE_COPY(qSlicerSegmentEditorLabelEffect);
};

#endif
