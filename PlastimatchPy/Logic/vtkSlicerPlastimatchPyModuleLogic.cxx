/*==============================================================================

  Program: 3D Slicer

  Portions (c) Copyright Brigham and Women's Hospital (BWH) All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

==============================================================================*/

#include "SlicerRtCommon.h"

// PlastimatchPy Logic includes
#include "vtkSlicerPlastimatchPyModuleLogic.h"

// MRML includes
#include <vtkMRMLAnnotationFiducialNode.h>
#include <vtkMRMLScalarVolumeNode.h>
#include <vtkMRMLLinearTransformNode.h>

// ITK includes
#include <itkAffineTransform.h>
#include <itkArray.h>
#include <itkImageRegionIteratorWithIndex.h>

// VTK includes
#include <vtkNew.h>
#include <vtkSmartPointer.h>
#include <vtkMatrix4x4.h>

// Plastimatch includes
#include "bspline_interpolate.h"
#include "plm_config.h"
#include "plm_image_header.h"
#include "plm_warp.h"
#include "plmregister.h"
#include "pointset.h"
#include "pointset_warp.h"
#include "xform.h"
#include "raw_pointset.h"
#include "volume.h"

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkSlicerPlastimatchPyModuleLogic);

//----------------------------------------------------------------------------
vtkSlicerPlastimatchPyModuleLogic::vtkSlicerPlastimatchPyModuleLogic()
{
  this->FixedImageID = NULL;
  this->MovingImageID = NULL;
  this->FixedLandmarksFileName = NULL;
  this->MovingLandmarksFileName = NULL;
  this->InputTransformationID = NULL;
  this->OutputVolumeID = NULL;

  this->FixedLandmarks = NULL;
  this->MovingLandmarks = NULL;

  vtkSmartPointer<vtkPoints> warpedLandmarks = vtkSmartPointer<vtkPoints>::New();
  this->SetWarpedLandmarks(warpedLandmarks);

  this->OutputVectorField = NULL;

  this->RegistrationParameters = new Registration_parms();
  this->RegistrationData = new Registration_data();
}

//----------------------------------------------------------------------------
vtkSlicerPlastimatchPyModuleLogic::~vtkSlicerPlastimatchPyModuleLogic()
{
  this->SetFixedImageID(NULL);
  this->SetMovingImageID(NULL);
  this->SetFixedLandmarksFileName(NULL);
  this->SetMovingLandmarksFileName(NULL);
  this->SetInputTransformationID(NULL);
  this->SetOutputVolumeID(NULL);

  this->SetFixedLandmarks(NULL);
  this->SetMovingLandmarks(NULL);
  this->SetWarpedLandmarks(NULL);

  this->OutputVectorField = NULL;

  this->RegistrationParameters = NULL;
  this->RegistrationData = NULL;
}

//----------------------------------------------------------------------------
void vtkSlicerPlastimatchPyModuleLogic::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//---------------------------------------------------------------------------
void vtkSlicerPlastimatchPyModuleLogic::SetMRMLSceneInternal(vtkMRMLScene * newScene)
{
  vtkNew<vtkIntArray> events;
  events->InsertNextValue(vtkMRMLScene::EndBatchProcessEvent);
  this->SetAndObserveMRMLSceneEventsInternal(newScene, events.GetPointer());
}

//-----------------------------------------------------------------------------
void vtkSlicerPlastimatchPyModuleLogic::RegisterNodes()
{
  if (!this->GetMRMLScene())
    {
    vtkErrorMacro("RegisterNodes: Invalid MRML Scene!");
    return;
    }
}

//---------------------------------------------------------------------------
void vtkSlicerPlastimatchPyModuleLogic::UpdateFromMRMLScene()
{
  if (!this->GetMRMLScene())
    {
    vtkErrorMacro("UpdateFromMRMLScene: Invalid MRML Scene!");
    return;
    }
}

//---------------------------------------------------------------------------
void vtkSlicerPlastimatchPyModuleLogic::AddStage()
{
  this->RegistrationParameters->append_stage();
}

//---------------------------------------------------------------------------
void vtkSlicerPlastimatchPyModuleLogic::SetPar(char* key, char* value)
{        
  this->RegistrationParameters->set_key_val(key, value, 1);
}

//---------------------------------------------------------------------------
void vtkSlicerPlastimatchPyModuleLogic::RunRegistration()
{
  // Set input images
  vtkMRMLVolumeNode* fixedVtkImage = vtkMRMLVolumeNode::SafeDownCast(this->GetMRMLScene()->GetNodeByID(this->FixedImageID));
  itk::Image<float, 3>::Pointer fixedItkImage = itk::Image<float, 3>::New();
  SlicerRtCommon::ConvertVolumeNodeToItkImageInLPS<float>(fixedVtkImage, fixedItkImage);

  vtkMRMLVolumeNode* movingVtkImage = vtkMRMLVolumeNode::SafeDownCast(this->GetMRMLScene()->GetNodeByID(this->MovingImageID));
  itk::Image<float, 3>::Pointer movingItkImage = itk::Image<float, 3>::New();
  SlicerRtCommon::ConvertVolumeNodeToItkImageInLPS<float>(movingVtkImage, movingItkImage);

  this->RegistrationData->fixed_image = new Plm_image(fixedItkImage);
  this->RegistrationData->moving_image = new Plm_image(movingItkImage);
  
  // Set landmarks 
  if (this->FixedLandmarks && this->MovingLandmarks)
    {
    // From Slicer
    this->SetLandmarksFromSlicer();
    }
  else if (this->FixedLandmarksFileName && this->FixedLandmarksFileName)
    {
    // From Files
    this->SetLandmarksFromFiles();
    }
  else
    {
    vtkErrorMacro("RunRegistration: Unable to retrieve fixed and moving landmarks!");
    return;
    }
  
  // Set initial affine transformation
  if (this->InputTransformationID)
    {
    this->ApplyInitialLinearTransformation(); 
    } 

  // Run registration and warp image
  Xform* outputTransformation = NULL; // Transformation (linear or deformable) computed by Plastimatch
  do_registration_pure(&outputTransformation, this->RegistrationData ,this->RegistrationParameters);

  Plm_image* warpedImage = new Plm_image();
  this->ApplyWarp(warpedImage, this->OutputVectorField, outputTransformation,
    this->RegistrationData->fixed_image, this->RegistrationData->moving_image, -1200, 0, 1);

  this->GetOutputImage(warpedImage);

  delete warpedImage;
}

//---------------------------------------------------------------------------
void vtkSlicerPlastimatchPyModuleLogic::WarpLandmarks()
{
  Labeled_pointset warpedPointset;
  pointset_warp(&warpedPointset, this->RegistrationData->moving_landmarks, this->OutputVectorField);
  
  // Clear warped landmarks
  this->WarpedLandmarks->Initialize();

  for (int i=0; i < (int)warpedPointset.count(); ++i)
  {
    vtkDebugMacro("[RTN] "
            << warpedPointset.point_list[i].p[0] << " "
            << warpedPointset.point_list[i].p[1] << " "
            << warpedPointset.point_list[i].p[2] << " -> "
            << -warpedPointset.point_list[i].p[0] << " "
            << -warpedPointset.point_list[i].p[1] << " "
            << warpedPointset.point_list[i].p[2]);
    this->WarpedLandmarks->InsertPoint(i,
      - warpedPointset.point_list[i].p[0],
      - warpedPointset.point_list[i].p[1],
      warpedPointset.point_list[i].p[2]);
    }
}

//---------------------------------------------------------------------------
void vtkSlicerPlastimatchPyModuleLogic::SetLandmarksFromSlicer()
{
  if (!this->FixedLandmarks || !this->MovingLandmarks)
    {
    vtkErrorMacro("SetLandmarksFromSlicer: Landmark point lists are not valid!");
    return;
    }

  Labeled_pointset* fixedLandmarksSet = new Labeled_pointset();
  Labeled_pointset* movingLandmarksSet = new Labeled_pointset();
  
  for (int i = 0; i < this->FixedLandmarks->GetNumberOfPoints(); i++)
    {
    Labeled_point* fixedLandmark = new Labeled_point("point",
      - this->FixedLandmarks->GetPoint(i)[0],
      - this->FixedLandmarks->GetPoint(i)[1],
      this->FixedLandmarks->GetPoint(i)[2]);

    Labeled_point* movingLandmark = new Labeled_point("point",
      - this->MovingLandmarks->GetPoint(i)[0],
      - this->MovingLandmarks->GetPoint(i)[1],
      this->MovingLandmarks->GetPoint(i)[2]);
   
    fixedLandmarksSet->point_list.push_back(*fixedLandmark);
    movingLandmarksSet->point_list.push_back(*movingLandmark);
    }

  this->RegistrationData->fixed_landmarks = fixedLandmarksSet;
  this->RegistrationData->moving_landmarks = movingLandmarksSet;
}

//---------------------------------------------------------------------------
void vtkSlicerPlastimatchPyModuleLogic::SetLandmarksFromFiles()
{
  if (!this->FixedLandmarksFileName || !this->MovingLandmarksFileName)
    {
    vtkErrorMacro("SetLandmarksFromFiles: Unable to read landmarks from files as at least one of the filenames is invalid!");
    return;
    }

  Labeled_pointset* fixedLandmarksFromFile = new Labeled_pointset();
  fixedLandmarksFromFile->load(this->FixedLandmarksFileName);
  this->RegistrationData->fixed_landmarks = fixedLandmarksFromFile;

  Labeled_pointset* movingLandmarksFromFile = new Labeled_pointset();
  movingLandmarksFromFile->load(this->MovingLandmarksFileName);
  this->RegistrationData->moving_landmarks = movingLandmarksFromFile;
}

//---------------------------------------------------------------------------
void vtkSlicerPlastimatchPyModuleLogic::ApplyInitialLinearTransformation()
{
  if (!this->InputTransformationID)
    {
    vtkErrorMacro("ApplyInitialLinearTransformation: Invalid input transformation ID!");
    return;
    }

  // Get transformation as 4x4 matrix
  vtkMRMLLinearTransformNode* inputTransformationNode =
    vtkMRMLLinearTransformNode::SafeDownCast(this->GetMRMLScene()->GetNodeByID(this->InputTransformationID));
  if (!inputTransformationNode)
    {
    vtkErrorMacro("ApplyInitialLinearTransformation: Failed to retrieve input transformation!");
    return;
    }
  vtkMatrix4x4* inputVtkTransformationMatrix = inputTransformationNode->GetMatrixTransformToParent();

  // Create ITK array to store the parameters
  itk::Array<double> affineParameters;
  affineParameters.SetSize(12);

  // Set rotations
  int affineParameterIndex=0;
  for (int column=0; column < 3; column++)
    {
    for (int row=0; row < 3; row++)
      {
      affineParameters.SetElement(affineParameterIndex, inputVtkTransformationMatrix->GetElement(row,column));
      affineParameterIndex++;
      }
    }

  // Set translations
  affineParameters.SetElement(9, inputVtkTransformationMatrix->GetElement(0,3));
  affineParameters.SetElement(10, inputVtkTransformationMatrix->GetElement(1,3));
  affineParameters.SetElement(11, inputVtkTransformationMatrix->GetElement(2,3));

  // Create ITK affine transformation
  itk::AffineTransform<double, 3>::Pointer inputItkTransformation = itk::AffineTransform<double, 3>::New();
  inputItkTransformation->SetParameters(affineParameters);

  // Set transformation
  Xform* inputTransformation = new Xform();
  inputTransformation->set_aff(inputItkTransformation);

  // Warp image using the input transformation
  Plm_image* outputImageFromInputTransformation = new Plm_image();
  this->ApplyWarp(outputImageFromInputTransformation, NULL, inputTransformation,
    this->RegistrationData->fixed_image, this->RegistrationData->moving_image, -1200, 0, 1);

  // Update moving image
  this->RegistrationData->moving_image = outputImageFromInputTransformation;

  delete inputTransformation;
}

//---------------------------------------------------------------------------
void vtkSlicerPlastimatchPyModuleLogic::ApplyWarp(Plm_image* warpedImage, DeformationFieldType::Pointer vectorFieldOut,
  Xform* inputTransformation, Plm_image* fixedImage, Plm_image* inputImage,
  float defaultValue, int useItk, int interpolationLinear)
{
  Plm_image_header* pih = new Plm_image_header(fixedImage);
  plm_warp(warpedImage, &vectorFieldOut, inputTransformation, pih, inputImage, defaultValue,
    useItk, interpolationLinear);
  this->OutputVectorField = vectorFieldOut;
}

//---------------------------------------------------------------------------
void vtkSlicerPlastimatchPyModuleLogic::GetOutputImage(Plm_image* warpedImage)
{
  if (!warpedImage || !warpedImage->itk_float())
    {
    vtkErrorMacro("GetOutputImage: Invalid warped image!");
    return;
    }

  itk::Image<float, 3>::Pointer outputImageItk = warpedImage->itk_float();    

  vtkSmartPointer<vtkImageData> outputImageVtk = vtkSmartPointer<vtkImageData>::New();
  itk::Image<float, 3>::RegionType region = outputImageItk->GetBufferedRegion();
  itk::Image<float, 3>::SizeType imageSize = region.GetSize();
  int extent[6]={0, (int) imageSize[0]-1, 0, (int) imageSize[1]-1, 0, (int) imageSize[2]-1};
  outputImageVtk->SetExtent(extent);
  outputImageVtk->SetScalarType(VTK_FLOAT);
  outputImageVtk->SetNumberOfScalarComponents(1);
  outputImageVtk->AllocateScalars();
  
  float* outputImagePtr = (float*)outputImageVtk->GetScalarPointer();
  itk::ImageRegionIteratorWithIndex< itk::Image<float, 3> > outputImageItkIterator(
    outputImageItk, outputImageItk->GetLargestPossibleRegion() );
  
  for ( outputImageItkIterator.GoToBegin(); !outputImageItkIterator.IsAtEnd(); ++outputImageItkIterator)
    {
    itk::Image<float, 3>::IndexType i = outputImageItkIterator.GetIndex();
    (*outputImagePtr) = outputImageItk->GetPixel(i);
    outputImagePtr++;
    }
  
  // Read fixed image to get the geometrical information
  vtkMRMLVolumeNode* fixedVolumeNode =
    vtkMRMLVolumeNode::SafeDownCast(this->GetMRMLScene()->GetNodeByID(this->FixedImageID));
  if (!fixedVolumeNode)
    {
    vtkErrorMacro("GetOutputImage: Node containing the fixed image cannot be retrieved!");
    return;
    }

  // Create new image node
  vtkMRMLVolumeNode* warpedImageNode =
    vtkMRMLVolumeNode::SafeDownCast(this->GetMRMLScene()->GetNodeByID(this->OutputVolumeID));
  if (!warpedImageNode)
    {
    vtkErrorMacro("GetOutputImage: Node containing the warped image cannot be retrieved!");
    return;
    }

  // Set warped image to a Slicer node
  warpedImageNode->CopyOrientation(fixedVolumeNode);
  warpedImageNode->SetAndObserveImageData(outputImageVtk);
}