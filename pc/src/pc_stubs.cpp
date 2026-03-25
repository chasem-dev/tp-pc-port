/* pc_stubs.cpp - catch-all stubs for remaining undefined symbols */
#include "pc_platform.h"
#include <dolphin/os/OSExec.h>

extern "C" {

/* OSExec globals - single definitions (declared extern in OSExec.h under TARGET_PC) */
OSExecParams* __OSExecParams = NULL;
s32 __OSAppLoaderOffset = 0;

/* GBA link */
void GBAInit(void) {}
s32 GBAGetStatus(s32 chan, u8* status) { (void)chan; (void)status; return -1; }
s32 GBARead(s32 chan, u8* dst, u8* status) { (void)chan; (void)dst; (void)status; return -1; }
s32 GBAWrite(s32 chan, u8* src, u8* status) { (void)chan; (void)src; (void)status; return -1; }
s32 GBAReset(s32 chan, u8* status) { (void)chan; (void)status; return -1; }

/* TRK (debugger) - all no-ops */
void TRK_main(void) {}
void __TRK_reset(void) {}

/* DMA */
void __OSCacheInit(void) {}

/* WPAD (Wii Remote) - all no-ops, these are Wii-specific */
void WPADControlMotor(s32 chan, u32 cmd) { (void)chan; (void)cmd; }
void WPADControlSpeaker(s32 chan, u32 cmd, void* callback) { (void)chan; (void)cmd; (void)callback; }
void WPADDisconnect(s32 chan) { (void)chan; }
u32 WPADGetAcceptConnection(void) { return 0; }
u32 WPADGetDpdSensitivity(void) { return 0; }
s32 WPADGetInfoAsync(s32 chan, void* info, void* callback) { (void)chan; (void)info; (void)callback; return -1; }
u32 WPADGetRadioSensitivity(void) { return 0; }
u8 WPADGetSpeakerVolume(void) { return 0; }
int WPADIsSpeakerEnabled(s32 chan) { (void)chan; return 0; }
s32 WPADProbe(s32 chan, u32* type) { (void)chan; if (type) *type = 0; return -1; }
void WPADRegisterAllocator(void* alloc, void* free) { (void)alloc; (void)free; }
void WPADSendStreamData(s32 chan, void* data, u16 len) { (void)chan; (void)data; (void)len; }
void WPADSetAcceptConnection(u32 accept) { (void)accept; }
void* WPADSetConnectCallback(s32 chan, void* callback) { (void)chan; (void)callback; return NULL; }
void* WPADSetExtensionCallback(s32 chan, void* callback) { (void)chan; (void)callback; return NULL; }
void WENCGetEncodeData(void* info, s32 flag, const s16* pcmData, s32 sampleNum, u8* encData) {
    (void)info; (void)flag; (void)pcmData; (void)sampleNum; (void)encData;
}

/* KPAD (Wii Remote higher-level API) */
void KPADInit(void) {}
void KPADDisableDPD(s32 chan) { (void)chan; }
void KPADEnableDPD(s32 chan) { (void)chan; }
s32 KPADRead(s32 chan, void* data, u32 num) { (void)chan; (void)data; (void)num; return 0; }
void KPADSetAccParam(s32 chan, f32 radius, f32 sensitivity) { (void)chan; (void)radius; (void)sensitivity; }
void KPADSetFSStickClamp(s8 min, s8 max) { (void)min; (void)max; }
void KPADSetObjInterval(f32 interval) { (void)interval; }
void KPADSetPosParam(s32 chan, f32 radius, f32 sensitivity) { (void)chan; (void)radius; (void)sensitivity; }
void KPADSetSensorHeight(s32 chan, f32 height) { (void)chan; (void)height; }

void stripint(int i) { (void)i; }

} /* extern "C" */

/* Data-stripping functions referenced by d_a_obj_thashi.cpp.
 * These are declared with C++ linkage in the callers (extern, no "C"). */
void stripFloat(f32 f) { (void)f; }
void stripDouble(f64 d) { (void)d; }
int getStripInt() { return 0; }

/* =========================================================================
 * C++ symbol stubs
 *
 * JSystem debug/tool symbols (HostIO, Audio HIO, Studio tools) and
 * game-specific symbols whose source files are excluded from the PC build.
 * ========================================================================= */

#include <dolphin/types.h>
#include "f_pc/f_pc_profile.h"
#include "f_op/f_op_actor.h"

/* -------------------------------------------------------------------------
 * JOR (JSystem HostIO Object Reflection) stubs
 *
 * The JHostIO source files are excluded from the PC build.  These classes
 * provide debug-tool communication with a host PC and are not needed at
 * runtime.
 * ----------------------------------------------------------------------- */

/* Forward declarations for types used in signatures */
class JORMContext;
class JORReflexible;
class JOREventListener;
struct JORPropertyEvent;
struct JORNodeEvent;
struct JORGenEvent;
struct JOREvent;
class JORFile;
class JORDir;
class JORHostInfo_String;
class JORHostInfo_CalendarTime;
class JORHostInfo;
class JSUMemoryInputStream;

/* --- JORServer --- */
#include "JSystem/JHostIO/JORServer.h"

JORServer* JORServer::instance = NULL;

JORServer* JORServer::create() { return NULL; }
void JORServer::receive(const char*, s32) {}
JORMContext* JORServer::attachMCTX(u32) { return NULL; }
void JORServer::releaseMCTX(JORMContext*) {}
void JORServer::defSetVal(void*, u32, s32) {}
void JORServer::defSetBitVal(void*, u32, u16, u16) {}
void JORServer::fio_openFile_(JSUMemoryInputStream&) {}
void JORServer::fio_closeFile_(JSUMemoryInputStream&) {}
void JORServer::fio_readData_(JSUMemoryInputStream&) {}
void JORServer::fio_writeData_(JSUMemoryInputStream&) {}
void JORServer::fio_dispatchMessage_(JSUMemoryInputStream&) {}
void JORServer::dir_findFirstFile_(JSUMemoryInputStream&, JORDir*) {}
void JORServer::dir_findNextFile_(JSUMemoryInputStream&, JORDir*) {}
void JORServer::dir_browseForFolder_(JSUMemoryInputStream&, JORDir*) {}
void JORServer::readResultS32_(JSUMemoryInputStream&) {}
void JORServer::readOrEvent_(JSUMemoryInputStream&) {}
void JORServer::dir_dispatchMessage_(JSUMemoryInputStream&) {}
void JORServer::hostinfo_dispatchMessage_(JSUMemoryInputStream&) {}
void JORServer::hostinfo_recvString_(JSUMemoryInputStream&, JORHostInfo_String*) {}
void JORServer::hostinfo_localTime_(JSUMemoryInputStream&, JORHostInfo_CalendarTime*) {}
void JORServer::readResultU32_(JSUMemoryInputStream&) {}
void JORServer::sendReset() {}
void JORServer::setRootNode(const char*, JORReflexible*, u32, u32) {}
void JORServer::doneEvent() {}

/* --- JOREventCallbackListNode --- */
JOREventCallbackListNode::JOREventCallbackListNode(u32, u32, bool) :
    m_node(), field_0xc(0), field_0x10(0) {}
JOREventCallbackListNode::~JOREventCallbackListNode() {}
int JOREventCallbackListNode::JORAct(u32, const char*) { return 0; }
void JOREventCallbackListNode::JORAppend() {}
void JOREventCallbackListNode::JORRemove() {}

/* --- JORMContext --- */
void JORMContext::genNodeSub(const char*, JORReflexible*, u32, u32) {}
void JORMContext::invalidNode(JORReflexible*, u32) {}
void JORMContext::genControl(u32, u32, const char*, u32, u32, JOREventListener*, u32) {}
void JORMContext::genSliderSub(u32, const char*, u32, u32, s32, s32, s32, JOREventListener*, u16, u16, u16, u16) {}
void JORMContext::genCheckBoxSub(u32, const char*, u32, u32, u16, u16, JOREventListener*, u16, u16, u16, u16) {}
void JORMContext::startSelectorSub(u32, u32, const char*, u32, u32, s32, JOREventListener*, u16, u16, u16, u16) {}
void JORMContext::endSelectorSub() {}
void JORMContext::genSelectorItemSub(const char*, s32, u32, u16, u16, u16, u16) {}
void JORMContext::genButton(const char*, u32, u32, JOREventListener*, u16, u16, u16, u16) {}
void JORMContext::genLabel(const char*, u32, u32, JOREventListener*, u16, u16, u16, u16) {}
void JORMContext::genGroupBox(const char*, u32, u32, JOREventListener*, u16, u16, u16, u16) {}
void JORMContext::genEditBoxID(const char*, u32, const char*, u16, u32, JOREventListener*, u16, u16, u16, u16) {}
void JORMContext::endNode() {}
void JORMContext::updateControl(u32, u32, u32) {}
void JORMContext::updateControl(u32, u32, const char*) {}
void JORMContext::updateSliderSub(u32, u32, s32, s32, s32, u32) {}
void JORMContext::updateCheckBoxSub(u32, u32, u16, u16, u32) {}
void JORMContext::updateSelectorSub(u32, u32, s32, u32) {}
void JORMContext::updateEditBoxID(u32, u32, const char*, u32, u16) {}
void JORMContext::editComboBoxItem(u32, u32, const char*, s32, u32) {}
void JORMContext::openMessageBox(void*, u32, const char*, const char*) {}
void JORMContext::openFile(JORFile*, u32, const char*, const char*, u32, const char*, const char*, const char*) {}
void JORMContext::closeFile(JORFile*) {}
void JORMContext::readBegin(JORFile*, s32) {}
void JORMContext::readData(JORFile*) {}
void JORMContext::writeBegin(JORFile*, u16, u32) {}
void JORMContext::writeData(JORFile*, const void*, s32, u32) {}
void JORMContext::writeDone(JORFile*, u32) {}
void JORMContext::sendShellExecuteRequest(void*, const char*, const char*, const char*, const char*, int) {}
void JORMContext::sendHostInfoRequest(u32, JORHostInfo*) {}

/* --- JORReflexible --- */
/* Note: JORReflexible methods are declared inside #if DEBUG in the header.
 * With DEBUG==0 these methods don't exist in the class, so we only provide
 * stubs when DEBUG is enabled. */
#if DEBUG
JORServer* JORReflexible::getJORServer() { return NULL; }
void JORReflexible::listenPropertyEvent(const JORPropertyEvent*) {}
void JORReflexible::listen(u32, const JOREvent*) {}
void JORReflexible::genObjectInfo(const JORGenEvent*) {}
void JORReflexible::listenNodeEvent(const JORNodeEvent*) {}
#endif

/* --- JOR free functions --- */
u32 JORMessageBox(const char*, const char*, u32) { return 0; }
int JORShellExecute(const char*, const char*, const char*, const char*, int) { return 0; }

/* -------------------------------------------------------------------------
 * JHI (JSystem HostIO Communication) stubs
 * ----------------------------------------------------------------------- */

/* JHIComm / JHIComPortManager / JHIhioASync -- these are template classes
 * whose methods are referenced indirectly through JORServer.  Provide the
 * minimum free-function stubs that the linker needs. */

/* JORInit / JHIEventLoop are referenced via inline functions in JORServer.h.
 * They may already have C++ declarations from the JHI headers. */
void JORInit() {}
u32 JHIEventLoop() { return 0; }

/* -------------------------------------------------------------------------
 * JAH (JSystem Audio HostIO) stubs
 * ----------------------------------------------------------------------- */
#include "JSystem/JAHostIO/JAHioNode.h"
#include "JSystem/JAHostIO/JAHFrameNode.h"
#include "JSystem/JAHostIO/JAHioMgr.h"

/* JAHioNode */
JAHioNode* JAHioNode::smCurrentNode = NULL;

JAHioNode::JAHioNode(const char* name) : mTree(this), mLastChild(NULL) {
    memset(mName, 0, sizeof(mName));
    if (name) strncpy(mName, name, sizeof(mName) - 1);
}
JAHioNode::~JAHioNode() {}
void JAHioNode::listenPropertyEvent(const JORPropertyEvent*) {}
void JAHioNode::genMessage(JORMContext*) {}
void JAHioNode::listenNodeEvent(const JORNodeEvent*) {}
void JAHioNode::appendNode(JAHioNode*, const char*) {}
void JAHioNode::prependNode(JAHioNode*, const char*) {}
void JAHioNode::removeNode(JAHioNode*) {}
u32 JAHioNode::getNodeKind() const { return 0; }
void JAHioNode::updateNode() {}
void JAHioNode::setNodeName(const char*) {}
void JAHioNode::generateRealChildren(JORMContext*) {}
void JAHioNode::generateTempChildren(JORMContext*) {}
JAHioNode* JAHioNode::getParent() { return NULL; }

/* JAHFrameNode */
JAHFrameNode::JAHFrameNode(const char* name) : JAHioNode(name), mTree(NULL), mFrameNodeLink(this) {}
JAHFrameNode::~JAHFrameNode() {}
void JAHFrameNode::listenPropertyEvent(const JORPropertyEvent*) {}
void JAHFrameNode::genMessage(JORMContext*) {}
void JAHFrameNode::listenNodeEvent(const JORNodeEvent*) {}
s32 JAHFrameNode::getNodeType() { return 0; }
void JAHFrameNode::onCurrentNodeFrame() {}
void JAHFrameNode::onFrame() {}
void JAHFrameNode::framework() {}
void JAHFrameNode::currentFramework() {}

/* JAHioMgr */
JAHioMgr::JAHioMgr() : field_0x4(0), field_0x8(0) {}
void JAHioMgr::init_OnGame() {}
bool JAHioMgr::isGameMode() { return false; }
void JAHioMgr::appendRootNode(JORReflexible*, JAHioNode*) {}
void JAHioMgr::appendFrameNode(JAHioNode*) {}
void JAHioMgr::removeFrameNode(JAHioNode*) {}
u32 JAHioMgr::framework() { return 0; }

/* JAHSingletonBase static instance for JAHioMgr */
template<> JAHioMgr* JAHSingletonBase<JAHioMgr>::sInstance = NULL;

/* JAHSoundPlayerNode */
#include "JSystem/JAHNodeLib/JAHSoundPlayerNode.h"
JAHSoundPlayerNode::JAHSoundPlayerNode() : JAHFrameNode("SoundPlayer") {}

/* -------------------------------------------------------------------------
 * JAW (JSystem Audio Workstation / WinLib) stubs
 *
 * These are debug audio tool windows. They all inherit from JAWWindow
 * which has a non-trivial constructor, but we only need the symbols to
 * link -- the constructors are never actually called at runtime.
 * ----------------------------------------------------------------------- */
#include "JSystem/JAWWinLib/JAWBankView.h"
#include "JSystem/JAWWinLib/JAWChView.h"
#include "JSystem/JAWWinLib/JAWEntrySeView.h"
#include "JSystem/JAWWinLib/JAWHioBankEdit.h"
#include "JSystem/JAWWinLib/JAWHioReceiver.h"
#include "JSystem/JAWWinLib/JAWPlayerChView.h"
#include "JSystem/JAWWinLib/JAWPlaySeView.h"
#include "JSystem/JAWWinLib/JAWReportView.h"
#include "JSystem/JAWWinLib/JAWSysMemView.h"
#include "JSystem/JAWWinLib/JAWVolume.h"

JAWBankView::JAWBankView() : JAWWindow("BankView", 400, 300) {}
JAWChView::JAWChView() : JAWWindow("ChView", 400, 300) {}
JAWEntrySeViewBasic::JAWEntrySeViewBasic() : JAWWindow("EntrySeView", 400, 300) {}
JAWHioBankEdit::JAWHioBankEdit() : JAWWindow("HioBankEdit", 400, 300) {}
JAWHioReceiver::JAWHioReceiver() : JAWWindow("HioReceiver", 400, 300) {}
JAWHioReceiver::THioReceiver::THioReceiver() {}
JAWHioReceiver::THioReceiver::~THioReceiver() {}
u32 JAWHioReceiver::THioReceiver::parse(u32, char*, u32) { return 0; }
bool JAWHioReceiver::TSeqList::getSeqData(JAISoundID, JAISeqData*) const { return false; }
JAWPlayerChView::JAWPlayerChView() : JAWWindow("PlayerChView", 400, 300) {}
JAWPlaySeViewBasic::JAWPlaySeViewBasic() : JAWWindow("PlaySeView", 400, 300) {}
JAWReportView::JAWReportView() : JAWWindow("ReportView", 400, 300) {}
JAWSysMemView::JAWSysMemView() : JAWWindow("SysMemView", 400, 300) {}
JAWVolume::JAWVolume(int w, int h) : JAWWindow("Volume", w, h) {}

/* JAWWindow base class -- has complex members (J2DWindow, J2DTextBox, TWindowText)
 * that don't have default constructors. Since this is a debug tool class never
 * actually used at runtime, we zero-init the object in the constructor. */
#include "JSystem/JAWExtSystem/JAWWindow.h"

/* Suppress missing-initializer errors for debug-only constructors */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"

JAWWindow::JAWWindow(const char*, int, int) :
    field_0x38(0, JGeometry::TBox2<f32>(0, 0, 0, 0), NULL),
    field_0x180(),
    field_0x2b0(this) {}
JAWWindow::~JAWWindow() {}
void JAWWindow::onDraw(JAWGraphContext*) {}
BOOL JAWWindow::onInit() { return FALSE; }
BOOL JAWWindow::initIf() { return FALSE; }
void JAWWindow::setTitleColor(const JUtility::TColor&, const JUtility::TColor&) {}
void JAWWindow::setWindowColor(const JUtility::TColor&, const JUtility::TColor&, const JUtility::TColor&, const JUtility::TColor&) {}
void JAWWindow::move(f32, f32) {}
void JAWWindow::addPosition(f32, f32) {}
void JAWWindow::addSize(f32, f32) {}
JUtility::TColor JAWWindow::convJudaColor(u16) { return JUtility::TColor(); }
void JAWWindow::padProc(const JUTGamePad&) {}
JAWWindow::TWindowText::TWindowText(JAWWindow* parent) :
    J2DPane(), field_0xfc(), m_pParent(parent), field_0x11c(0, 0) {}
JAWWindow::TWindowText::~TWindowText() {}
void JAWWindow::TWindowText::drawSelf(f32, f32) {}
void JAWWindow::TWindowText::drawSelf(f32, f32, Mtx*) {}

#pragma clang diagnostic pop

/* JAWSystemInterface base class */
#include "JSystem/JAWExtSystem/JAWSystem.h"

JAWSystemInterface::JAWSystemInterface() : mHeap(NULL) {}
JKRHeap* JAWSystemInterface::getCurrentHeap() const { return mHeap; }
JAWSystemInterface* JAWSystemInterface::sInstance = NULL;

/* JAWExtSystem namespace functions (defined in JAWExtSystem.cpp which is excluded) */
#include "JSystem/JAWExtSystem/JAWExtSystem.h"

namespace JAWExtSystem {
    BOOL registWindow(u32, JAWWindow*, int, int) { return FALSE; }
    BOOL destroyWindow(u32, JAWWindow*) { return FALSE; }
    void nextPage() {}
    void prevPage() {}
    void nextWindow() {}
    void prevWindow() {}
    void draw() {}
    void padProc(const JUTGamePad&) {}

    JGadget::TList<JAWWindow*> sPage[128];
    JAWExtSystem::TSystemInterface sInterface;
    s32 sCurrentPage = 0;
    u8 lbl_80748E44 = 0;
}

/* JAWGraphContext */
#include "JSystem/JAWExtSystem/JAWGraphContext.h"

JAWGraphContext::JAWGraphContext() : field_0x0(NULL), mParentAlpha(255), field_0x15(0), field_0x16(0), field_0x18(0) {}
JAWGraphContext::~JAWGraphContext() {}
void JAWGraphContext::reset() {}
void JAWGraphContext::color(u8, u8, u8, u8) {}
void JAWGraphContext::color(const JUtility::TColor&) {}
void JAWGraphContext::locate(int, int) {}
void JAWGraphContext::print(char const*, ...) {}
void JAWGraphContext::print(int, int, const char*, ...) {}
void JAWGraphContext::color(const JUtility::TColor&, const JUtility::TColor&, const JUtility::TColor&, const JUtility::TColor&) {}
void JAWGraphContext::fillBox(const JGeometry::TBox2<f32>&) {}
void JAWGraphContext::drawFrame(const JGeometry::TBox2<f32>&) {}
void JAWGraphContext::line(const JGeometry::TVec2<f32>&, const JGeometry::TVec2<f32>&) {}
void JAWGraphContext::setGXforPrint() {}
void JAWGraphContext::setGXforDraw() {}
JUTResFont* JAWGraphContext::sFont = NULL;
bool JAWGraphContext::lbl_8074CD30 = false;

/* JADHioReceiver */
JADHioReceiver::JADHioReceiver() : JHITag<JHICmnMem>(0) {}
JADHioReceiver::~JADHioReceiver() {}
void JADHioReceiver::receive(const char*, s32) {}

/* -------------------------------------------------------------------------
 * JStudio tool (editor / previewer) stubs
 *
 * d_jcam_editor.cpp and d_jpreviewer.cpp are excluded from the PC build
 * because JStudioCameraEditor and JStudioPreviewer classes have extremely
 * deep dependency chains into empty decomp source files.
 * The JStudioPreviewer constructor is in the JSystem source; we provide
 * the remaining undefined methods here.
 * ----------------------------------------------------------------------- */
#include "JSystem/JStudio/JStudio_JPreviewer/control.h"

void JStudioPreviewer::TControl::genMessage(JORMContext*) {}
void JStudioPreviewer::TControl::jstudio_setControl(JStudio::TControl*) {}
void JStudioPreviewer::TControl::jstudio_setParse(JStudio::TParse*) {}
void JStudioPreviewer::TControl::update() {}
void JStudioPreviewer::TControl::show2D() {}
void JStudioPreviewer::TControl::show3D(Mtx) {}
int JStudioPreviewer::TControl::JORAct(u32, const char*) { return 0; }

/* JStudioCameraEditor and JStudioToolLibrary -- source files are all empty
 * stubs in the decomp. These are complex debug editor classes with deep
 * dependency chains. Rather than fighting with non-default-constructible
 * members, we exclude d_jcam_editor.cpp and d_jpreviewer.cpp from the
 * build and provide the minimal symbols they export. */

/* JStudioToolLibrary types -- their source files are empty in the decomp */
#include "JSystem/JStudio/JStudioToolLibrary/interface.h"
#include "JSystem/JStudio/JStudioToolLibrary/visual.h"

namespace JStudioToolLibrary {
    TPad::TPad() : pPad_(NULL) {}
    bool TPad::isEnabled() const { return false; }
    void TPad::getData(TData*) const {}
    f32 TPad::getAnalog_triggerR() const { return 0; }
    f32 TPad::getAnalog_triggerL() const { return 0; }
    f32 TPad::getAnalog_subStickY() const { return 0; }
    f32 TPad::getAnalog_subStickX() const { return 0; }
    f32 TPad::getAnalog_mainStickY() const { return 0; }
    f32 TPad::getAnalog_mainStickX() const { return 0; }
    int TPad::getButton_repeat() const { return 0; }
    int TPad::getButton_release() const { return 0; }
    int TPad::getButton_trigger() const { return 0; }
    int TPad::getButton() const { return 0; }
    TPad::TData::TData() { memset(this, 0, sizeof(*this)); }
    void TPad::TData::reset() { memset(this, 0, sizeof(*this)); }

    TRectangle::TRectangle() : iLeft_(0), iTop_(0), iRight_(0), iBottom_(0) {}
    TRectangle::TRectangle(int l, int t, int r, int b) : iLeft_(l), iTop_(t), iRight_(r), iBottom_(b) {}
    void TRectangle::set(int l, int t, int r, int b) { iLeft_=l; iTop_=t; iRight_=r; iBottom_=b; }
    int TRectangle::getBottom() const { return iBottom_; }
    int TRectangle::getTop() const { return iTop_; }
    int TRectangle::getRight() const { return iRight_; }
    int TRectangle::getLeft() const { return iLeft_; }
    int TRectangle::getWidth() const { return iRight_ - iLeft_; }
    int TRectangle::getHeight() const { return iBottom_ - iTop_; }

    TDrawPrimitive2D::TDrawPrimitive2D() : pOrthoGraph_(NULL) {}
    void TDrawPrimitive2D::fillRectangle(int, int, int, int) {}
    void TDrawPrimitive2D::setColor(const JUtility::TColor&) {}
    void TDrawPrimitive2D::prepare() {}
    bool TDrawPrimitive2D::isEnabled() const { return false; }
    TRectangle TDrawPrimitive2D::getRectangle() const { return TRectangle(); }
    void TDrawPrimitive2D::setColor(const JUtility::TColor&, const JUtility::TColor&, const JUtility::TColor&, const JUtility::TColor&) {}
    void TDrawPrimitive2D::frameRectangle(int, int, int, int) {}
    void TDrawPrimitive2D::setLineWidth(f32) {}

    TDrawPrimitive3D::TDrawPrimitive3D() : fLineWidth_(1.0f) { memset(&oColor_, 0, sizeof(oColor_)); }
    void TDrawPrimitive3D::setColor(GXColor) {}
    void TDrawPrimitive3D::setLineWidth(f32) {}
    void TDrawPrimitive3D::setGXState_position3f32() {}
    void TDrawPrimitive3D::endGX() {}
    void TDrawPrimitive3D::beginGX(GXPrimitive, u32) {}
    void TDrawPrimitive3D::setGXColor(GXColor) {}
    void TDrawPrimitive3D::setGXLineWidth(f32) {}
    void TDrawPrimitive3D::prepare() {}
    void TDrawPrimitive3D::drawAxis() {}
    void TDrawPrimitive3D::setGXColor() {}
    void TDrawPrimitive3D::setGXLineWidth() {}
    void TDrawPrimitive3D::setGXState_position1x8() {}
    void TDrawPrimitive3D::drawAxis_arrow() {}
    void TDrawPrimitive3D::drawAxis_color() {}
    void TDrawPrimitive3D::setGXState_position1x8_color1x8() {}
    void TDrawPrimitive3D::drawAxis_color_arrow() {}
    void TDrawPrimitive3D::drawGrid_xyz(uint) {}
    void TDrawPrimitive3D::setGXState_position3s16() {}
    void TDrawPrimitive3D::drawGrid_xy(uint) {}
    void TDrawPrimitive3D::drawGrid_xz(uint) {}
    void TDrawPrimitive3D::drawGrid_yz(uint) {}

    TPrint::TPrint() : pFont_(NULL), field_0x4(0), field_0x8(0), iX_(0), iY_(0) {}
    int TPrint::getY() const { return iY_; }
    int TPrint::getX() const { return iX_; }
    void TPrint::setColor(const JUtility::TColor&) {}
    void TPrint::locate(int, int) {}
    void TPrint::prepare() {}
    int TPrint::getFontHeight() const { return 0; }
    int TPrint::getFontWidth() const { return 0; }
    bool TPrint::isEnabled() const { return false; }
    void TPrint::processControlCharacter(int) {}
    void TPrint::print(int) {}
    void TPrint::drawCharacter(int) {}
    void TPrint::print(const char*) {}
    void TPrint::print_f(const char*, ...) {}
    void TPrint::print_f_va(const char*, va_list) {}

    TColor_variable::TColor_variable(const JUtility::TColor& c, const TVelocity& v)
        : oColor_(c), oVelocity_(v) {}
    void TColor_variable::update() {}
    void TColor_variable::updateValue_(u8*, int*) {}
    TColor_variable::TVelocity::TVelocity(const TVelocity& o)
        : field_0x0(o.field_0x0), field_0x4(o.field_0x4), field_0x8(o.field_0x8), field_0xc(o.field_0xc) {}
    TColor_variable::TVelocity::TVelocity(int a, int b, int c, int d)
        : field_0x0(a), field_0x4(b), field_0x8(c), field_0xc(d) {}
}

/* -------------------------------------------------------------------------
 * Z2 (Zelda 2 / TP-specific) debug stubs
 * ----------------------------------------------------------------------- */
#include "Z2AudioLib/Z2DebugSys.h"
#include "Z2AudioLib/Z2F1TestWindow.h"
#include "Z2AudioLib/Z2FxLineMgr.h"
#include "Z2AudioLib/Z2TrackView.h"
#include "Z2AudioLib/Z2WaveArcLoader.h"

/* Z2DebugSys -- constructor and methods are defined unconditionally in
 * Z2DebugSys.cpp which is part of the build. No stubs needed. */

/* Z2ParamNode -- virtual method overrides are declared but never defined
 * in the decomp source. The vtable requires them. */
void Z2ParamNode::message(JAHControl&) {}
void Z2ParamNode::propertyEvent(JAH_P_Event, u32) {}
void Z2ParamNode::onFrame() {}

/* Z2 audio debug views */
Z2AudSettingView::Z2AudSettingView() : JAWWindow("Z2AudSettingView", 400, 300) {}
Z2F1SwingTestNode::Z2F1SwingTestNode() : JAHFrameNode("Z2F1SwingTest"), Z2F1TestWindow() {}
Z2F1TestWindow::Z2F1TestWindow() : JAWWindow("Z2F1TestWindow", 400, 300) {}
Z2FxLineEditNode::Z2FxLineEditNode(JKRExpHeap*) : JAHFrameNode("Z2FxLineEdit") {}
Z2TrackView::Z2TrackView(u8, DispMode) : JAWWindow("Z2TrackView", 400, 300) {}
Z2WaveArcLoader::Z2WaveArcLoader() : JAWWindow("Z2WaveArcLoader", 400, 300) {}

/* Z2FxLineMgr -- fully defined in Z2FxLineMgr.cpp, no stubs needed. */

/* -------------------------------------------------------------------------
 * mDoHIO (game HostIO wrappers) stubs
 *
 * The entire m_Do_hostIO.cpp is inside #if DEBUG, which evaluates to 0
 * in the PC build (NDEBUG is defined).  Provide the symbols that are
 * referenced outside of #if DEBUG guards.
 * ----------------------------------------------------------------------- */
#include "m_Do/m_Do_hostIO.h"

mDoHIO_root_c mDoHIO_root;
mDoHIO_root_c::~mDoHIO_root_c() {}
mDoHIO_subRoot_c::~mDoHIO_subRoot_c() {}
mDoHIO_child_c::~mDoHIO_child_c() {}

void mDoHIO_root_c::genMessage(JORMContext*) {}
void mDoHIO_subRoot_c::genMessage(JORMContext*) {}
void mDoHIO_root_c::update() {}
void mDoHIO_root_c::updateChild(s8) {}
void mDoHIO_root_c::deleteChild(s8) {}
s8 mDoHIO_subRoot_c::createChild(const char*, JORReflexible*) { return -1; }
void mDoHIO_subRoot_c::deleteChild(s8) {}
void mDoHIO_subRoot_c::updateChild(s8) {}

/* mDoHIO_entry_c: constructor, entryHIO, removeHIO, and non-inline destructor
 * are only declared when DEBUG is enabled. In non-debug, only the inline
 * virtual destructor exists. */
#if DEBUG
mDoHIO_entry_c::mDoHIO_entry_c() { mNo = -1; mCount = 0; }
mDoHIO_entry_c::~mDoHIO_entry_c() {}
void mDoHIO_entry_c::entryHIO(const char*) {}
void mDoHIO_entry_c::removeHIO() {}
#endif

void mDoHIO_deleteChild(s8) {}
void mDoHIO_updateChild(s8) {}

/* -------------------------------------------------------------------------
 * fapGm_HIO (game HIO settings) stubs
 * ----------------------------------------------------------------------- */
#include "f_ap/f_ap_game.h"

/* Static member definitions for fapGm_HIO_c.
 * The constructor is defined in f_ap_game.cpp (unconditionally compiled).
 * The static members below are declared in the header unconditionally but
 * only defined in f_ap_game.cpp inside #if DEBUG.  We need definitions
 * when DEBUG==0. */
#if !DEBUG
u8 fapGm_HIO_c::m_CpuTimerOn = 0;
u8 fapGm_HIO_c::m_CpuTimerOff = 0;
u8 fapGm_HIO_c::m_CpuTimerStart = 0;
u32 fapGm_HIO_c::m_CpuTimerTick = 0;
CaptureScreen* fapGm_HIO_c::mCaptureScreen = NULL;
void* fapGm_HIO_c::mCaptureScreenBuffer = NULL;
s16 fapGm_HIO_c::mCaptureScreenFlag = 0;
u16 fapGm_HIO_c::mCaptureScreenWidth = 0;
u16 fapGm_HIO_c::mCaptureScreenHeight = 0;
u16 fapGm_HIO_c::mCaptureScreenLinePf = 0;
u16 fapGm_HIO_c::mCaptureScreenLineNum = 0;
u8 fapGm_HIO_c::mCaptureScreenNumH = 0;
u8 fapGm_HIO_c::mCaptureScreenNumV = 0;
u8 fapGm_HIO_c::mParticle254Fix = 0;
u8 fapGm_HIO_c::mCaptureMagnification = 1;
u8 fapGm_HIO_c::mCaptureScreenDivH = 1;
u8 fapGm_HIO_c::mCaptureScreenDivV = 1;
u8 fapGm_HIO_c::mPackArchiveMode = 1;

/* These methods are declared unconditionally but defined only in #if DEBUG */
void fapGm_HIO_c::startCpuTimer() {}
void fapGm_HIO_c::stopCpuTimer(const char*) {}
void fapGm_HIO_c::printCpuTimer(const char*) {}
#endif

/* fapGm_dataMem -- methods and mCsv are declared unconditionally in the
 * header but defined inside #if DEBUG in f_ap_game.cpp. */
#if !DEBUG
char fapGm_dataMem::mCsv[0x8000] = {0};
void fapGm_dataMem::printfTag(int, int, int, const char*, void*, u32, const char*, const char*) {}
int fapGm_dataMem::findParentHeap(void*) { return 0; }
void fapGm_dataMem::dumpTag() {}
void fapGm_dataMem::dumpCsv() {}
#endif

/* -------------------------------------------------------------------------
 * Profile stubs for excluded actor source files
 *
 * d_a_mg_rod.cpp and d_a_movie_player.cpp are excluded due to switch/case
 * cross-initialization issues. Their profiles are still referenced by the
 * profile list in f_pc_profile_lst.cpp.
 * ----------------------------------------------------------------------- */

static process_method_class stub_methods = { NULL, NULL, NULL, NULL };

process_profile_definition g_profile_MG_ROD = {
    /* layer_id     */ 0xFFFFFFFE, /* fpcLy_CURRENT_e */
    /* list_id      */ 8,
    /* list_priority*/ 0,
    /* name         */ 0,
    /* methods      */ &stub_methods,
    /* process_size */ 0,
    /* unk_size     */ 0,
    /* parameters   */ 0,
};

process_profile_definition g_profile_MOVIE_PLAYER = {
    /* layer_id     */ 0xFFFFFFFE, /* fpcLy_CURRENT_e */
    /* list_id      */ 7,
    /* list_priority*/ 0,
    /* name         */ 0,
    /* methods      */ &stub_methods,
    /* process_size */ 0,
    /* unk_size     */ 0,
    /* parameters   */ 0,
};

/* =========================================================================
 * Category 2: mDoExt debug packet constructors/destructors
 *
 * These are inside #if DEBUG in m_Do_ext.cpp, so with NDEBUG they are
 * not compiled. Provide stubs here.
 * ========================================================================= */
#include "m_Do/m_Do_ext.h"

#if !DEBUG
mDoExt_cube8pPacket::mDoExt_cube8pPacket(cXyz* i_points, const GXColor& i_color) {
    if (i_points) {
        for (int i = 0; i < 8; i++) mPoints[i] = i_points[i];
    }
    mColor = i_color;
}
void mDoExt_cube8pPacket::draw() {}

mDoExt_cubePacket::mDoExt_cubePacket(cXyz& i_position, cXyz& i_size, csXyz& i_angle, const GXColor& i_color) {
    mPosition = i_position;
    mSize = i_size;
    mAngle = i_angle;
    mColor = i_color;
}
void mDoExt_cubePacket::draw() {}

mDoExt_trianglePacket::mDoExt_trianglePacket(cXyz* i_points, const GXColor& i_color, u8 i_clipZ) {
    if (i_points) {
        mPoints[0] = i_points[0];
        mPoints[1] = i_points[1];
        mPoints[2] = i_points[2];
    }
    mColor = i_color;
    mClipZ = i_clipZ;
}
void mDoExt_trianglePacket::draw() {}

mDoExt_quadPacket::mDoExt_quadPacket(cXyz* i_points, const GXColor& i_color, u8 i_clipZ) {
    if (i_points) {
        for (int i = 0; i < 4; i++) mPoints[i] = i_points[i];
    }
    mColor = i_color;
    mClipZ = i_clipZ;
}
void mDoExt_quadPacket::draw() {}

mDoExt_linePacket::mDoExt_linePacket(cXyz& i_start, cXyz& i_end, const GXColor& i_color, u8 i_clipZ, u8 i_width) {
    mStart = i_start;
    mEnd = i_end;
    mColor = i_color;
    mClipZ = i_clipZ;
    mWidth = i_width;
}
void mDoExt_linePacket::draw() {}

mDoExt_ArrowPacket::mDoExt_ArrowPacket(cXyz& i_position, cXyz& param_1, const GXColor& i_color, u8 i_clipZ, u8 i_lineWidth) {
    mStart = i_position;
    mEnd = param_1;
    mColor = i_color;
    mClipZ = i_clipZ;
    mLineWidth = i_lineWidth;
}
void mDoExt_ArrowPacket::draw() {}

mDoExt_pointPacket::mDoExt_pointPacket(cXyz& i_position, const GXColor& i_color, u8 i_clipZ, u8 i_lineWidth) {
    mPosition = i_position;
    mColor = i_color;
    mClipZ = i_clipZ;
    mLineWidth = i_lineWidth;
}
void mDoExt_pointPacket::draw() {}

mDoExt_circlePacket::mDoExt_circlePacket(cXyz& i_position, f32 i_radius, const GXColor& i_color, u8 i_clipZ, u8 i_lineWidth) {
    mPosition = i_position;
    mRadius = i_radius;
    mColor = i_color;
    mClipZ = i_clipZ;
    mLineWidth = i_lineWidth;
}
void mDoExt_circlePacket::draw() {}

mDoExt_spherePacket::mDoExt_spherePacket(cXyz& i_position, f32 i_size, const GXColor& i_color, u8 i_clipZ) {
    mPosition = i_position;
    mSize = i_size;
    mColor = i_color;
    mClipZ = i_clipZ;
}
void mDoExt_spherePacket::draw() {}

mDoExt_cylinderPacket::mDoExt_cylinderPacket(cXyz& i_position, f32 i_radius, f32 i_height, const GXColor& i_color, u8 i_clipZ) {
    mPosition = i_position;
    mRadius = i_radius;
    mHeight = i_height;
    mColor = i_color;
    mClipZ = i_clipZ;
}
void mDoExt_cylinderPacket::draw() {}

mDoExt_cylinderMPacket::mDoExt_cylinderMPacket(Mtx i_mtx, const GXColor& i_color, u8 i_clipZ) {
    memcpy(mMatrix, i_mtx, sizeof(Mtx));
    mColor = i_color;
    mClipZ = i_clipZ;
}
void mDoExt_cylinderMPacket::draw() {}

/* offCupOnAup / onCupOffAup: draw() is outside DEBUG, but destructor needs stub */
mDoExt_offCupOnAupPacket::~mDoExt_offCupOnAupPacket() {}
mDoExt_onCupOffAupPacket::~mDoExt_onCupOffAupPacket() {}
#endif /* !DEBUG */

/* =========================================================================
 * Category 3: JGadget stream members
 *
 * These functions are declared in the JGadget headers but never defined
 * in the decomp source files.
 * ========================================================================= */
#include "JSystem/JGadget/std-stream.h"

namespace JGadget {
    void TStream_base::width(s32 w) { width_ = w; }

    bool TStream::fail() const { return (state_ & 0x6) != 0; }
    bool TStream::good() const { return state_ == 0; }
    TStreamBuffer* TStream::rdbuf() const { return rdbuf_; }
    void TStream::setstate(u8 state) { state_ |= state; }
    char TStream::widen(char c) { return c; }
    void TStream::fill(char c) { fill_ = c; }
    void TStream::clear(u8 state) { state_ = state; }
    char TStream::narrow(char c, char) { return c; }

    void TStream_base::setf(u32 flags) { flags_ |= flags; }
    void TStream_base::setf(u32 flags, u32 mask) { flags_ = (flags_ & ~mask) | (flags & mask); }

    int TStreamBuffer::sgetc() {
        if (pCurrent_get_ < pEnd_get_) {
            return TTrait_char<char>::to_int_type(*pCurrent_get_);
        }
        return underflow();
    }
}

/* =========================================================================
 * Category 4: JAHVirtualNode
 *
 * Virtual node class for Audio HostIO.
 * ========================================================================= */
#include "JSystem/JAHostIO/JAHVirtualNode.h"

JAHVirtualNode::JAHVirtualNode() : mTree(this) {
    memset(mName, 0, sizeof(mName));
}
void JAHVirtualNode::updateNode() {}
void JAHVirtualNode::message(JAHControl&) {}
void JAHVirtualNode::onFrame() {}
void JAHVirtualNode::onCurrentNodeFrame() {}
void JAHVirtualNode::propertyEvent(JAH_P_Event, u32) {}
void JAHVirtualNode::nodeEvent(JAH_N_Event) {}

/* JAHUSeBox constructor */
#include "JSystem/JAHostIO/JAHUTableEdit.h"
JAHUSeBox::JAHUSeBox() {}

/* =========================================================================
 * Category 5: Miscellaneous remaining symbols
 * ========================================================================= */

/* --- g_kankyoHIO ---
 * g_kankyoHIO is defined in d_kankyo.cpp inside #if DEBUG which is 0.
 * d_kankyo_debug.cpp references it unconditionally. Provide a global instance
 * and the constructor + methods that are defined inside #if DEBUG. */
#include "d/d_kankyo.h"

#if !DEBUG
/* All dKankyo sub-HIO class constructors and methods.
 * These are defined inside #if DEBUG in d_kankyo.cpp. */
dKankyo_ParticlelightHIO_c::dKankyo_ParticlelightHIO_c() {}
void dKankyo_ParticlelightHIO_c::listenPropertyEvent(const JORPropertyEvent*) {}
void dKankyo_ParticlelightHIO_c::genMessage(JORMContext*) {}

dKankyo_lightHIO_c::dKankyo_lightHIO_c() {}
void dKankyo_lightHIO_c::listenPropertyEvent(const JORPropertyEvent*) {}
void dKankyo_lightHIO_c::genMessage(JORMContext*) {}
void dKankyo_lightHIO_c::dKankyo_lightHIOInfoUpDateF() {}

dKankyo_vrboxHIO_c::dKankyo_vrboxHIO_c() {}
void dKankyo_vrboxHIO_c::listenPropertyEvent(const JORPropertyEvent*) {}
void dKankyo_vrboxHIO_c::genMessage(JORMContext*) {}
void dKankyo_vrboxHIO_c::dKankyo_vrboxHIOInfoUpDateF() {}

dKankyo_bloomHIO_c::dKankyo_bloomHIO_c() {}
void dKankyo_bloomHIO_c::listenPropertyEvent(const JORPropertyEvent*) {}
void dKankyo_bloomHIO_c::genMessage(JORMContext*) {}

dKankyo_navyHIO_c::dKankyo_navyHIO_c() {}
void dKankyo_navyHIO_c::genMessage(JORMContext*) {}

dKankyo_efflightHIO_c::dKankyo_efflightHIO_c() {}
void dKankyo_efflightHIO_c::genMessage(JORMContext*) {}

dKankyo_demolightHIO_c::dKankyo_demolightHIO_c() {}
void dKankyo_demolightHIO_c::genMessage(JORMContext*) {}

dKankyo_dungeonlightHIO_c::dKankyo_dungeonlightHIO_c() {}
void dKankyo_dungeonlightHIO_c::listenPropertyEvent(const JORPropertyEvent*) {}
void dKankyo_dungeonlightHIO_c::genMessage(JORMContext*) {}

dKankyo_windHIO_c::dKankyo_windHIO_c() {}
void dKankyo_windHIO_c::genMessage(JORMContext*) {}

dKankyo_HIO_c::dKankyo_HIO_c() {}
void dKankyo_HIO_c::genMessage(JORMContext*) {}

dKankyo_HIO_c g_kankyoHIO;
#endif

/* --- JUTResFONT_Ascfont_fix16 ---
 * Font resource data referenced by JAWWindow constructor for debug text.
 * Provide a minimal dummy array. */
#include "JSystem/JUtility/JUTResFont.h"
u8 const JUTResFONT_Ascfont_fix16[16] = {0};

/* --- J3DPSMtxArrayConcat ---
 * PowerPC paired-single assembly function. Provide a C fallback. */
#include "JSystem/J3DGraphBase/J3DTransform.h"

#ifndef __MWERKS__
void J3DPSMtxArrayConcat(Mtx mA, Mtx mB, Mtx mAB, u32 count) {
    /* Each iteration: mAB[i] = mA × mB[i] (mA is constant for all iterations) */
    for (u32 i = 0; i < count; i++) {
        f32 (*a)[4] = (f32(*)[4])mA;
        f32 (*b)[4] = (f32(*)[4])((u8*)mB + i * 48);
        f32 (*ab)[4] = (f32(*)[4])((u8*)mAB + i * 48);
        f32 tmp[3][4];
        for (int r = 0; r < 3; r++) {
            tmp[r][0] = a[r][0]*b[0][0] + a[r][1]*b[1][0] + a[r][2]*b[2][0];
            tmp[r][1] = a[r][0]*b[0][1] + a[r][1]*b[1][1] + a[r][2]*b[2][1];
            tmp[r][2] = a[r][0]*b[0][2] + a[r][1]*b[1][2] + a[r][2]*b[2][2];
            tmp[r][3] = a[r][0]*b[0][3] + a[r][1]*b[1][3] + a[r][2]*b[2][3] + a[r][3];
        }
        memcpy(ab, tmp, 48);
    }
}
#endif

/* --- JKRHeap::sRootHeap2 ---
 * Static member defined in JKRHeap.cpp only under PLATFORM_WII/PLATFORM_SHIELD.
 * On PC neither macro is defined, so provide the definition here. */
#include "JSystem/JKernel/JKRHeap.h"
JKRHeap* JKRHeap::sRootHeap2 = NULL;

/* --- JSUMemoryOutputStream missing virtuals ---
 * getPosition(), seek(), getAvailable() are declared in the header
 * as virtual overrides but not defined in JSUMemoryStream.cpp. */
#include "JSystem/JSupport/JSUMemoryStream.h"

s32 JSUMemoryOutputStream::getPosition() const {
    return mPosition;
}

s32 JSUMemoryOutputStream::seek(s32 offset, JSUStreamSeekFrom whence) {
    return seekPos(offset, whence);
}

s32 JSUMemoryOutputStream::getAvailable() const {
    return mLength - mPosition;
}

/* --- JASGlobalInstance<Z2DebugSys>::sInstance ---
 * Template static member. The AUDIO_INSTANCES macro declares it but
 * the definition only occurs in files that instantiate Z2DebugSys. */
#include "JSystem/JAudio2/JASGadget.h"

class Z2DebugSys;
template<> Z2DebugSys* JASGlobalInstance<Z2DebugSys>::sInstance = NULL;

/* --- F(float*) ---
 * Mystery function referenced by d_a_horse.cpp. Likely a dead-code
 * artifact. Provide an empty stub with C++ linkage. */
void F(f32* p) { (void)p; }

/* --- dMsgObject_c::setWord / setSelectWord ---
 * Static methods declared in the header but never defined in d_msg_object.cpp.
 * They are called by inline wrapper functions. */
#include "d/d_msg_object.h"

void dMsgObject_c::setWord(const char* i_word) {
    dMsgObject_c* obj = dComIfGp_getMsgObjectClass();
    if (obj) obj->setWordLocal(i_word);
}

void dMsgObject_c::setSelectWord(int i_no, const char* i_word) {
    dMsgObject_c* obj = dComIfGp_getMsgObjectClass();
    if (obj) obj->setSelectWordLocal(i_no, i_word);
}

/* --- dummy_child_class vtable ---
 * Used in d_menu_calibration.cpp as a local class with a pure virtual override.
 * Since it's defined as a static local, we just need the vtable symbol. */
class dummy_abstract_class {
public:
    virtual void virt_func_0() = 0;
};
class dummy_child_class : public dummy_abstract_class {
public:
    virtual void virt_func_0() {}
};
/* Force vtable emission */
static dummy_child_class g_dummy_child_instance;

/* --- OSSwitchFiberEx ---
 * Already defined in pc_os.cpp. If still unresolved, it may need C linkage.
 * (Checked: pc_os.cpp defines it in extern "C", so it should link.) */

/* --- stripFloat, stripDouble, getStripInt ---
 * Already defined above in the extern "C" block. */
