#include <sys/time.h>
#include <stdexcept>
#include <new>
#include <cstring>
#include <stdio.h>
#include "ofxImageEffect.h"
#include "ofxMemory.h"
#include "ofxMultiThread.h"
#include "ofxTimeLine.h"

#include "../include/ofxUtilities.H" // example support utils

#if defined __APPLE__ || defined linux || defined __FreeBSD__
#  define EXPORT __attribute__((visibility("default")))
#elif defined _WIN32
#  define EXPORT OfxExport
#else
#  error Not building on your operating system quite yet
#endif

typedef unsigned long long Uint64;

Uint64 StartCount;

void TimerReset()
{
    StartCount=0;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    StartCount=Uint64(tv.tv_sec)*1000000 + Uint64(tv.tv_usec);
}

Uint64 GetMilliSeconds()
{
    Uint64 Count;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    Count=Uint64(tv.tv_sec)*1000000 + Uint64(tv.tv_usec);
    Uint64 Diff = Count - StartCount;
    return Uint64(((double)Diff)/1000.0);
}

#define LOG_IN       printf("%llu: Enter %s\n", GetMilliSeconds(), __FUNCTION__)
#define LOG_OUT      printf("%llu: Leave %s\n", GetMilliSeconds(), __FUNCTION__)
#define LOG_STR(str) printf("%s='%s'\n", #str, (str));
#define LOG_INT(v)   printf("%s='%d'\n", #v, int(v));

template <class T> inline T Maximum(T a, T b) {return a > b ? a : b;}
template <class T> inline T Minimum(T a, T b) {return a < b ? a : b;}

// pointers64 to various bits of the host
OfxHost                 *gHost;
OfxImageEffectSuiteV1 *gEffectHost = 0;
OfxPropertySuiteV1    *gPropHost = 0;
OfxParameterSuiteV1   *gParamHost = 0;
OfxMultiThreadSuiteV1 *gThreadHost = 0;
OfxMemorySuiteV1      *gMemoryHost = 0;
OfxMessageSuiteV1     *gMessageSuite = 0;
OfxInteractSuiteV1    *gInteractHost = 0;

// some flags about the host's behaviour
int gHostSupportsMultipleBitDepths = false;

// private instance data type
struct MyInstanceData
{
  // handles to the clips we deal with
  OfxImageClipHandle sourceClip;
  OfxImageClipHandle outputClip;

  // handles to a our parameters
  OfxParamHandle scaleParam;
  OfxParamHandle prepareButtonParam;
  OfxParamHandle adjustButtonParam;
  OfxParamHandle triggerParam;
};

static MyInstanceData *getMyInstanceData(OfxImageEffectHandle effect)
{
  // get the property handle for the plugin
  OfxPropertySetHandle effectProps;
  gEffectHost->getPropertySet(effect, &effectProps);

  // get my data pointer out of that
  MyInstanceData *myData = 0;
  gPropHost->propGetPointer(effectProps,  kOfxPropInstanceData, 0, (void **) &myData);
  return myData;
}

// Convinience wrapper to set the enabledness of a parameter
static inline void setParamEnabledness( OfxImageEffectHandle effect,
                    const char *paramName,
                    int enabledState)
{
  LOG_IN;
  // fetch the parameter set for this effect
  OfxParamSetHandle paramSet;
  gEffectHost->getParamSet(effect, &paramSet);

  // fetch the parameter property handle
  OfxParamHandle param; OfxPropertySetHandle paramProps;
  gParamHost->paramGetHandle(paramSet, paramName, &param, &paramProps);

  // and set its enabledness
  gPropHost->propSetInt(paramProps,  kOfxParamPropEnabled, 0, enabledState);
  LOG_OUT;
}

static OfxStatus onLoad(void)
{
  LOG_IN;
  TimerReset();
  LOG_OUT;
  return kOfxStatOK;
}

static OfxStatus onUnLoad(void)
{
  LOG_IN;
  LOG_OUT;
  return kOfxStatOK;
}

static OfxStatus createInstance( OfxImageEffectHandle effect)
{
  LOG_IN;
  // get a pointer to the effect properties
  OfxPropertySetHandle effectProps;
  gEffectHost->getPropertySet(effect, &effectProps);

  // get a pointer to the effect's parameter set
  OfxParamSetHandle paramSet;
  gEffectHost->getParamSet(effect, &paramSet);

  // make my private instance data
  MyInstanceData *myData = new MyInstanceData;

  // cache away out param handles
  gParamHost->paramGetHandle(paramSet, "scale", &myData->scaleParam, 0);
  gParamHost->paramGetHandle(paramSet, "prepareButton", &myData->prepareButtonParam, 0);
  gParamHost->paramGetHandle(paramSet, "adjustButton", &myData->adjustButtonParam, 0);
  gParamHost->paramGetHandle(paramSet, "trigger", &myData->triggerParam, 0);

  // cache away out clip handles
  gEffectHost->clipGetHandle(effect, kOfxImageEffectSimpleSourceClipName, &myData->sourceClip, 0);
  gEffectHost->clipGetHandle(effect, kOfxImageEffectOutputClipName, &myData->outputClip, 0);

  gPropHost->propSetPointer(effectProps, kOfxPropInstanceData, 0, (void *) myData);

  setParamEnabledness(effect, "adjustButton", 0);

  LOG_OUT;

  return kOfxStatOK;
}

static OfxStatus destroyInstance(OfxImageEffectHandle  effect)
{
  LOG_IN;

  MyInstanceData *myData = getMyInstanceData(effect);
  if(myData) delete myData;

  LOG_OUT;
  return kOfxStatOK;
}

static OfxStatus isIdentity( OfxImageEffectHandle  effect, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)
{
  LOG_IN;

  OfxTime time;
  gPropHost->propGetDouble(inArgs, kOfxPropTime, 0, &time);

  MyInstanceData *myData = getMyInstanceData(effect);

  double scaleValue;
  gParamHost->paramGetValueAtTime(myData->scaleParam, time, &scaleValue);

  if(scaleValue == 1.0)
  {
    gPropHost->propSetString(outArgs, kOfxPropName, 0, kOfxImageEffectSimpleSourceClipName);
    return kOfxStatOK;
  }

  LOG_OUT;
  return kOfxStatReplyDefault;
}

static OfxStatus instanceChanged(OfxImageEffectHandle  effect, OfxPropertySetHandle inArgs, OfxPropertySetHandle /*outArgs*/)
{
  LOG_IN;
  // see why it changed
  char *changeReason;
  gPropHost->propGetString(inArgs, kOfxPropChangeReason, 0, &changeReason);

  // we are only interested in user edits
  if(strcmp(changeReason, kOfxChangeUserEdited) != 0) return kOfxStatReplyDefault;

  // fetch the type of the object that changed
  char *typeChanged;
  gPropHost->propGetString(inArgs, kOfxPropType, 0, &typeChanged);

  // was it a clip or a param?
  bool isClip = strcmp(typeChanged, kOfxTypeClip) == 0;
  bool isParam = strcmp(typeChanged, kOfxTypeParameter) == 0;

  // get the name of the thing that changed
  char *objChanged;
  gPropHost->propGetString(inArgs, kOfxPropName, 0, &objChanged);

  LOG_STR(objChanged);
  LOG_STR(typeChanged);
  LOG_STR(changeReason);

  if(isParam && strcmp(objChanged, "prepareButton")  == 0)
  {
    setParamEnabledness(effect, "adjustButton", 1);
    return kOfxStatOK;
  }

  LOG_OUT;
  // don't trap any others
  return kOfxStatReplyDefault;
}

////////////////////////////////////////////////////////////////////////////////
// rendering routines
template <class T> inline T 
Clamp(T v, int min, int max)
{
  if(v < T(min)) return T(min);
  if(v > T(max)) return T(max);
  return v;
}

// look up a pixel in the image, does bounds checking to see if it is in the image rectangle
template <class PIX> inline PIX *
pixelAddress(PIX *img, OfxRectI rect, int x, int y, int bytesPerLine)
{  
  if(x < rect.x1 || x >= rect.x2 || y < rect.y1 || y >= rect.y2 || !img)
    return 0;
  PIX *pix = (PIX *) (((char *) img) + (y - rect.y1) * bytesPerLine);
  pix += x - rect.x1;  
  return pix;
}

////////////////////////////////////////////////////////////////////////////////
// base class to process images with
class Processor {
 protected :
  OfxImageEffectHandle  instance;
  float         rScale, gScale, bScale, aScale;
  void *srcV, *dstV;
  OfxRectI srcRect, dstRect;
  int srcBytesPerLine, dstBytesPerLine;
  OfxRectI  window;

 public :
  Processor(OfxImageEffectHandle  inst,
            float rScal, float gScal, float bScal, float aScal,
            void *src, OfxRectI sRect, int sBytesPerLine,
            void *dst, OfxRectI dRect, int dBytesPerLine,
            OfxRectI  win)
    : instance(inst)
    , rScale(rScal)
    , gScale(gScal)
    , bScale(bScal)
    , aScale(aScal)
    , srcV(src)
    , dstV(dst)
    , srcRect(sRect)
    , dstRect(dRect)
    , srcBytesPerLine(sBytesPerLine)
    , dstBytesPerLine(dBytesPerLine)
    , window(win)
  {}

  static void multiThreadProcessing(unsigned int threadId, unsigned int nThreads, void *arg);
  virtual void doProcessing(OfxRectI window) = 0;
  void process(void);
};


// function call once for each thread by the host
void
Processor::multiThreadProcessing(unsigned int threadId, unsigned int nThreads, void *arg)
{
  Processor *proc = (Processor *) arg;

  // slice the y range into the number of threads it has
  unsigned int dy = proc->window.y2 - proc->window.y1;

  unsigned int y1 = proc->window.y1 + threadId * dy/nThreads;
  unsigned int y2 = proc->window.y1 + Minimum((threadId + 1) * dy/nThreads, dy);

  OfxRectI win = proc->window;
  win.y1 = y1; win.y2 = y2;

  // and render that thread on each
  proc->doProcessing(win);
}

// function to kick off rendering across multiple CPUs
void
Processor::process(void)
{
  unsigned int nThreads;
  gThreadHost->multiThreadNumCPUs(&nThreads);
  gThreadHost->multiThread(multiThreadProcessing, nThreads, (void *) this);
}

// template to do the RGBA processing
template <class PIX, class ELEMENT, int max, int isFloat>
class ProcessRGBA : public Processor{
public :
  ProcessRGBA(OfxImageEffectHandle  instance,
          float rScale, float gScale, float bScale, float aScale,
          void *srcV, OfxRectI srcRect, int srcBytesPerLine,
          void *dstV, OfxRectI dstRect, int dstBytesPerLine,
          OfxRectI  window)
    : Processor(instance,
                rScale, gScale, bScale, aScale,
                srcV,  srcRect,  srcBytesPerLine,
                dstV,  dstRect,  dstBytesPerLine,
                window)
  {
  }

  void doProcessing(OfxRectI procWindow)
  {
    PIX *src = (PIX *) srcV;
    PIX *dst = (PIX *) dstV;

    for(int y = procWindow.y1; y < procWindow.y2; y++) {
      if(gEffectHost->abort(instance)) break;

      PIX *dstPix = pixelAddress(dst, dstRect, procWindow.x1, y, dstBytesPerLine);

      for(int x = procWindow.x1; x < procWindow.x2; x++) {

        PIX *srcPix = pixelAddress(src, srcRect, x, y, srcBytesPerLine);

        // figure the scale values per component
        float sR = 1.0 + (rScale - 1.0);// * maskV;
        float sG = 1.0 + (gScale - 1.0);// * maskV;
        float sB = 1.0 + (bScale - 1.0);// * maskV;
        float sA = 1.0 + (aScale - 1.0);// * maskV;

        if(srcPix) {
          // switch will be compiled out
          if(isFloat) {
            dstPix->r = srcPix->r * sR;
            dstPix->g = srcPix->g * sG;
            dstPix->b = srcPix->b * sB;
            dstPix->a = srcPix->a * sA;
          }
          else {
            dstPix->r = Clamp(int(srcPix->r * sR), 0, max);
            dstPix->g = Clamp(int(srcPix->g * sG), 0, max);
            dstPix->b = Clamp(int(srcPix->b * sB), 0, max);
            dstPix->a = Clamp(int(srcPix->a * sA), 0, max);
          }
          srcPix++;
        }
        dstPix++;
      }
    }
  }
};

// the process code  that the host sees
static OfxStatus render( OfxImageEffectHandle  instance,
                         OfxPropertySetHandle inArgs,
                         OfxPropertySetHandle /*outArgs*/)
{
  LOG_IN;
  // get the render window and the time from the inArgs
  OfxTime time;
  OfxRectI renderWindow;
  OfxStatus status = kOfxStatOK;

  gPropHost->propGetDouble(inArgs, kOfxPropTime, 0, &time);
  gPropHost->propGetIntN(inArgs, kOfxImageEffectPropRenderWindow, 4, &renderWindow.x1);

  // retrieve any instance data associated with this effect
  MyInstanceData *myData = getMyInstanceData(instance);

  // property handles and members of each image
  // in reality, we would put this in a struct as the C++ support layer does
  OfxPropertySetHandle sourceImg = NULL, outputImg = NULL;//, maskImg = NULL;
  int srcRowBytes, srcBitDepth, dstRowBytes, dstBitDepth;//, maskRowBytes = 0, maskBitDepth;
  bool srcIsAlpha, dstIsAlpha;
  OfxRectI dstRect, srcRect;
  void *src, *dst;

  try {
    // get the source image
    sourceImg = ofxuGetImage(myData->sourceClip, time+5, srcRowBytes, srcBitDepth, srcIsAlpha, srcRect, src);
    if(sourceImg == NULL) throw OfxuNoImageException();

    // get the output image
    outputImg = ofxuGetImage(myData->outputClip, time, dstRowBytes, dstBitDepth, dstIsAlpha, dstRect, dst);
    if(outputImg == NULL) throw OfxuNoImageException();

    // see if they have the same depths and bytes and all
    if(srcBitDepth != dstBitDepth || srcIsAlpha != dstIsAlpha) {
      throw OfxuStatusException(kOfxStatErrImageFormat);
    }

    // get the scale parameters
    double scale, rScale = 1, gScale = 1, bScale = 1, aScale = 1;
    gParamHost->paramGetValueAtTime(myData->scaleParam, time, &scale);
    rScale = scale; gScale = scale; bScale = scale; aScale = scale;

    // do the rendering
    if(!dstIsAlpha) {
      switch(dstBitDepth) {
      case 8 : {
        ProcessRGBA<OfxRGBAColourB, unsigned char, 255, 0> fred(instance, rScale, gScale, bScale, aScale,
                                                                src, srcRect, srcRowBytes,
                                                                dst, dstRect, dstRowBytes,
                                                                //mask, maskRect, maskRowBytes,
                                                                renderWindow);
        fred.process();
      }
        break;

      case 16 : {
        ProcessRGBA<OfxRGBAColourS, unsigned short, 65535, 0> fred(instance, rScale, gScale, bScale, aScale,
                                                                   src, srcRect, srcRowBytes,
                                                                   dst, dstRect, dstRowBytes,
                                                                   //mask, maskRect, maskRowBytes,
                                                                   renderWindow);
        fred.process();
      }
        break;

      case 32 : {
        ProcessRGBA<OfxRGBAColourF, float, 1, 1> fred(instance, rScale, gScale, bScale, aScale,
                                                      src, srcRect, srcRowBytes,
                                                      dst, dstRect, dstRowBytes,
                                                      //mask, maskRect, maskRowBytes,
                                                      renderWindow);
        fred.process();
        break;
      }
      }
    }
  }
  catch(OfxuNoImageException &ex) {
    // if we were interrupted, the failed fetch is fine, just return kOfxStatOK
    // otherwise, something wierd happened
    if(!gEffectHost->abort(instance)) {
      status = kOfxStatFailed;
    }
  }
  catch(OfxuStatusException &ex) {
    status = ex.status();
  }

  // release the data pointers
  if(sourceImg)
    gEffectHost->clipReleaseImage(sourceImg);
  if(outputImg)
    gEffectHost->clipReleaseImage(outputImg);

//  setParamEnabledness(instance, "adjustButton", 0);
  //OfxStatus r = gParamHost->paramSetValue(myData->prepareButtonParam, "prepareButton", "click!");
  //LOG_INT(r);
  LOG_OUT;

  return status;
}

// convience function to define scaling parameter
static void
defineScaleParam( OfxParamSetHandle effectParams,
                 const char *name,
                 const char *label,
                 const char *scriptName,
                 const char *hint,
                 const char *parent)
{
  OfxPropertySetHandle props;
  OfxStatus stat;
  stat = gParamHost->paramDefine(effectParams, kOfxParamTypeDouble, name, &props);
  if (stat != kOfxStatOK) {
    throw OfxuStatusException(stat);
  }
  // say we are a scaling parameter
  gPropHost->propSetString(props, kOfxParamPropDoubleType, 0, kOfxParamDoubleTypeScale);
  gPropHost->propSetDouble(props, kOfxParamPropDefault, 0, 1.0);
  gPropHost->propSetDouble(props, kOfxParamPropMin, 0, 0.0);
  gPropHost->propSetDouble(props, kOfxParamPropDisplayMin, 0, 0.0);
  gPropHost->propSetDouble(props, kOfxParamPropDisplayMax, 0, 100.0);
  gPropHost->propSetString(props, kOfxParamPropHint, 0, hint);
  gPropHost->propSetString(props, kOfxParamPropScriptName, 0, scriptName);
  gPropHost->propSetString(props, kOfxPropLabel, 0, label);
  if(parent)
    gPropHost->propSetString(props, kOfxParamPropParent, 0, parent);
}

//  describe the plugin in context
static OfxStatus describeInContext(OfxImageEffectHandle  effect,  OfxPropertySetHandle inArgs)
{
  OfxPropertySetHandle props;
  // define the single output clip in both contexts
  gEffectHost->clipDefine(effect, kOfxImageEffectOutputClipName, &props);

  // set the component types we can handle on out output
  gPropHost->propSetString(props, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);
  gPropHost->propSetString(props, kOfxImageEffectPropSupportedComponents, 1, kOfxImageComponentAlpha);

  // define the single source clip in both contexts
  gEffectHost->clipDefine(effect, kOfxImageEffectSimpleSourceClipName, &props);

  // set the component types we can handle on our main input
  gPropHost->propSetString(props, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);
  gPropHost->propSetString(props, kOfxImageEffectPropSupportedComponents, 1, kOfxImageComponentAlpha);

  OfxParamSetHandle paramSet;
  gEffectHost->getParamSet(effect, &paramSet);

  // overall scale param
  defineScaleParam(paramSet, "scale", "scale", "scale", "Scales all component in the image", 0);

  gParamHost->paramDefine(paramSet, kOfxParamTypePushButton, "prepareButton", &props);
  gPropHost->propSetString(props, kOfxPropLabel, 0, "Prepare");
  gPropHost->propSetString(props, kOfxParamPropScriptName, 0, "prepareButton");

  gParamHost->paramDefine(paramSet, kOfxParamTypePushButton, "adjustButton", &props);
  gPropHost->propSetString(props, kOfxPropLabel, 0, "Adjust");
  gPropHost->propSetString(props, kOfxParamPropScriptName, 0, "adjustButton");

  gParamHost->paramDefine(paramSet, kOfxParamTypeBoolean, "trigger", &props);
  gPropHost->propSetInt(props, kOfxParamPropDefault, 0, 0);
  gPropHost->propSetString(props, kOfxParamPropScriptName, 0, "trigger");
  gPropHost->propSetString(props, kOfxPropLabel, 0, "trigger");

  // make a page of controls and add my parameters to it
  gParamHost->paramDefine(paramSet, kOfxParamTypePage, "Main", &props);
  gPropHost->propSetString(props, kOfxParamPropPageChild, 0, "scale");
  gPropHost->propSetString(props, kOfxParamPropPageChild, 1, "prepareButton");
  gPropHost->propSetString(props, kOfxParamPropPageChild, 2, "adjustButton");
  gPropHost->propSetString(props, kOfxParamPropPageChild, 3, "trigger");

  return kOfxStatOK;
}

static OfxStatus describe(OfxImageEffectHandle  effect)
{
  // first fetch the host APIs, this cannot be done before this call
  OfxStatus stat;
  if((stat = ofxuFetchHostSuites()) != kOfxStatOK)
    return stat;

  // get the property handle for the plugin
  OfxPropertySetHandle effectProps;
  gEffectHost->getPropertySet(effect, &effectProps);

  // We can render both fields in a fielded images in one hit if there is no animation
  // So set the flag that allows us to do this
  gPropHost->propSetInt(effectProps, kOfxImageEffectPluginPropFieldRenderTwiceAlways, 0, 0);

  // say we can support multiple pixel depths and let the clip preferences action deal with it all.
  gPropHost->propSetInt(effectProps, kOfxImageEffectPropSupportsMultipleClipDepths, 0, 0);
  
  // set the bit depths the plugin can handle
  gPropHost->propSetString(effectProps, kOfxImageEffectPropSupportedPixelDepths, 2, kOfxBitDepthFloat);

  // set some labels and the group it belongs to
  gPropHost->propSetString(effectProps, kOfxPropLabel, 0, "OFX Gain Example");
  gPropHost->propSetString(effectProps, kOfxImageEffectPluginPropGrouping, 0, "OFX Example");

  // define the contexts we can be used in
  gPropHost->propSetString(effectProps, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextFilter);

  gPropHost->propSetInt(effectProps, kOfxImageEffectPropTemporalClipAccess, 0, 1);

  return kOfxStatOK;
}

static OfxStatus pluginMain(const char *action,  const void *handle, OfxPropertySetHandle inArgs,  OfxPropertySetHandle outArgs)
{
  LOG_IN;
  LOG_STR(action);
  try {
  // cast to appropriate type
  OfxImageEffectHandle effect = (OfxImageEffectHandle) handle;

  if(strcmp(action, kOfxActionDescribe) == 0) {
    return describe(effect);
  }
  else if(strcmp(action, kOfxImageEffectActionDescribeInContext) == 0) {
    return describeInContext(effect, inArgs);
  }
  else if(strcmp(action, kOfxActionLoad) == 0) {
    return onLoad();
  }
  else if(strcmp(action, kOfxActionUnload) == 0) {
    return onUnLoad();
  }
  else if(strcmp(action, kOfxActionCreateInstance) == 0) {
    return createInstance(effect);
  } 
  else if(strcmp(action, kOfxActionDestroyInstance) == 0) {
    return destroyInstance(effect);
  } 
  else if(strcmp(action, kOfxImageEffectActionIsIdentity) == 0) {
    return isIdentity(effect, inArgs, outArgs);
  }    
  else if(strcmp(action, kOfxImageEffectActionRender) == 0) {
    return render(effect, inArgs, outArgs);
  }    
  else if(strcmp(action, kOfxActionInstanceChanged) == 0) {
    return instanceChanged(effect, inArgs, outArgs);
  }  
  } catch (std::bad_alloc) {
    // catch memory
    //std::cout << "OFX Plugin Memory error." << std::endl;
    return kOfxStatErrMemory;
  } catch ( const std::exception& e ) {
    // standard exceptions
    //std::cout << "OFX Plugin error: " << e.what() << std::endl;
    return kOfxStatErrUnknown;
  } catch (int err) {
    // ho hum, gone wrong somehow
    return err;
  } catch ( ... ) {
    // everything else
    //std::cout << "OFX Plugin error" << std::endl;
    return kOfxStatErrUnknown;
  }

  // other actions to take the default value
  return kOfxStatReplyDefault;
}

// function to set the host structure
static void setHostFunc(OfxHost *hostStruct)
{
  gHost         = hostStruct;
}

static OfxPlugin basicPlugin = 
{
  kOfxImageEffectPluginApi,
  1,
  "uk.co.thefoundry.BasicGainPlugin",
  1,
  0,
  setHostFunc,
  pluginMain
};

// the two mandated functions
EXPORT OfxPlugin *OfxGetPlugin(int nth)
{
  if(nth == 0)
    return &basicPlugin;
  return 0;
}

EXPORT int OfxGetNumberOfPlugins(void)
{
  return 1;
}
