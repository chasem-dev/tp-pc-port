#include "JSystem/JSystem.h" // IWYU pragma: keep

#include "JSystem/JFramework/JFWSystem.h"
#include "JSystem/JKernel/JKRExpHeap.h"
#include "JSystem/JUtility/JUTConsole.h"
#include "JSystem/JUtility/JUTGraphFifo.h"
#include "JSystem/JKernel/JKRAram.h"
#include "JSystem/JUtility/JUTVideo.h"
#include "JSystem/JUtility/JUTGamePad.h"
#include "JSystem/JUtility/JUTDirectPrint.h"
#include "JSystem/JUtility/JUTAssert.h"
#include "JSystem/JUtility/JUTException.h"
#include "JSystem/JUtility/JUTResFont.h"
#include "JSystem/JUtility/JUTDbPrint.h"

s32 JFWSystem::CSetUpParam::maxStdHeaps = 2;

u32 JFWSystem::CSetUpParam::sysHeapSize = 0x400000;

JKRExpHeap* JFWSystem::rootHeap;

JKRExpHeap* JFWSystem::systemHeap;


u32 JFWSystem::CSetUpParam::fifoBufSize = 0x40000;

u32 JFWSystem::CSetUpParam::aramAudioBufSize = 0x800000;

u32 JFWSystem::CSetUpParam::aramGraphBufSize = 0x600000;

s32 JFWSystem::CSetUpParam::streamPriority = 8;

s32 JFWSystem::CSetUpParam::decompPriority = 7;

s32 JFWSystem::CSetUpParam::aPiecePriority = 6;

ResFONT* JFWSystem::CSetUpParam::systemFontRes = (ResFONT*)&JUTResFONT_Ascfont_fix12;

const GXRenderModeObj* JFWSystem::CSetUpParam::renderMode = &GXNtsc480IntDf;

u32 JFWSystem::CSetUpParam::exConsoleBufferSize = 0x24FC;

void JFWSystem::firstInit() {
    JUT_ASSERT(80, rootHeap == NULL);
    OSInit();
    DVDInit();
    rootHeap = JKRExpHeap::createRoot(CSetUpParam::maxStdHeaps, false);
    systemHeap = JKRExpHeap::create(CSetUpParam::sysHeapSize, rootHeap, false);
}

JKRThread* JFWSystem::mainThread;

JUTDbPrint* JFWSystem::debugPrint;

JUTResFont* JFWSystem::systemFont;

JUTConsoleManager* JFWSystem::systemConsoleManager;

JUTConsole* JFWSystem::systemConsole;

bool JFWSystem::sInitCalled = false;

void JFWSystem::init() {
    JUT_ASSERT(101, sInitCalled == false);

    if (rootHeap == NULL) {
#ifdef TARGET_PC
        fprintf(stderr, "[PC] JFWSystem::init: firstInit...\n");
#endif
        firstInit();
    }
    sInitCalled = true;

#ifdef TARGET_PC
    fprintf(stderr, "[PC] JFWSystem::init: JKRAram::create...\n");
#endif
    JKRAram::create(CSetUpParam::aramAudioBufSize, CSetUpParam::aramGraphBufSize,
                    CSetUpParam::streamPriority, CSetUpParam::decompPriority,
                    CSetUpParam::aPiecePriority);
#ifdef TARGET_PC
    fprintf(stderr, "[PC] JFWSystem::init: mainThread wrapper...\n");
#endif
    mainThread = new JKRThread(OSGetCurrentThread(), 4);

#ifdef TARGET_PC
    fprintf(stderr, "[PC] JFWSystem::init: JUTVideo::createManager...\n");
#endif
    JUTVideo::createManager(CSetUpParam::renderMode);
#ifdef TARGET_PC
    fprintf(stderr, "[PC] JFWSystem::init: JUTCreateFifo...\n");
#endif
    JUTCreateFifo(CSetUpParam::fifoBufSize);

#ifdef TARGET_PC
    fprintf(stderr, "[PC] JFWSystem::init: JUTGamePad::init...\n");
#endif
    JUTGamePad::init();

#ifdef TARGET_PC
    fprintf(stderr, "[PC] JFWSystem::init: JUTDirectPrint::start...\n");
#endif
    JUTDirectPrint* dbPrint = JUTDirectPrint::start();

#ifdef TARGET_PC
    fprintf(stderr, "[PC] JFWSystem::init: JUTAssertion::create...\n");
#endif
    JUTAssertion::create();

#ifdef TARGET_PC
    fprintf(stderr, "[PC] JFWSystem::init: JUTException::create...\n");
#endif
    JUTException::create(dbPrint);

#ifdef TARGET_PC
    fprintf(stderr, "[PC] JFWSystem::init: system font...\n");
#endif
    systemFont = new JUTResFont(CSetUpParam::systemFontRes, NULL);

#ifdef TARGET_PC
    fprintf(stderr, "[PC] JFWSystem::init: JUTDbPrint::start...\n");
#endif
    debugPrint = JUTDbPrint::start(NULL, NULL);
    debugPrint->changeFont(systemFont);

#ifdef TARGET_PC
    fprintf(stderr, "[PC] JFWSystem::init: console manager...\n");
#endif
    systemConsoleManager = JUTConsoleManager::createManager(NULL);

#ifdef TARGET_PC
    fprintf(stderr, "[PC] JFWSystem::init: console create...\n");
#endif
    systemConsole = JUTConsole::create(60, 200, NULL);
    systemConsole->setFont(systemFont);

    if (CSetUpParam::renderMode->efbHeight < 300) {
        systemConsole->setFontSize(systemFont->getWidth() * 0.85f, systemFont->getHeight() * 0.5f);
        systemConsole->setPosition(20, 25);
    } else {
        systemConsole->setFontSize(systemFont->getWidth(), systemFont->getHeight());
        systemConsole->setPosition(20, 50);
    }

    systemConsole->setHeight(25);
    systemConsole->setVisible(false);
    systemConsole->setOutput(JUTConsole::OUTPUT_OSREPORT | JUTConsole::OUTPUT_CONSOLE);
    JUTSetReportConsole(systemConsole);
    JUTSetWarningConsole(systemConsole);

#ifdef TARGET_PC
    fprintf(stderr, "[PC] JFWSystem::init: exception console...\n");
#endif
    void* buffer = systemHeap->alloc(CSetUpParam::exConsoleBufferSize, 4);
    JUTException::createConsole(buffer, CSetUpParam::exConsoleBufferSize);
#ifdef TARGET_PC
    fprintf(stderr, "[PC] JFWSystem::init: complete\n");
#endif
}
