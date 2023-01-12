/*
 * tkMacOSXWindowEvent.c --
 *
 *	This file defines the routines for both creating and handling Window
 *	Manager class events for Tk.
 *
 * Copyright 2001-2009, Apple Inc.
 * Copyright (c) 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright (c) 2015 Kevin Walzer/WordTech Communications LLC.
 * Copyright (c) 2015 Marc Culler.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkMacOSXPrivate.h"
#include "tkMacOSXWm.h"
#include "tkMacOSXEvent.h"
#include "tkMacOSXDebug.h"
 
/*
#ifdef TK_MAC_DEBUG
#define TK_MAC_DEBUG_EVENTS
#define TK_MAC_DEBUG_DRAWING
#endif
*/

/*
 * Declaration of functions used only in this file
 */

static int		GenerateUpdates(HIShapeRef updateRgn,
			    CGRect *updateBounds, TkWindow *winPtr);
static int		GenerateActivateEvents(TkWindow *winPtr,
			    int activeFlag);
static void		DoWindowActivate(ClientData clientData);

#pragma mark TKApplication(TKWindowEvent)

#ifdef TK_MAC_DEBUG_NOTIFICATIONS
extern NSString *NSWindowWillOrderOnScreenNotification;
extern NSString *NSWindowDidOrderOnScreenNotification;
extern NSString *NSWindowDidOrderOffScreenNotification;

#if MAC_OS_X_VERSION_MIN_REQUIRED < 1060
#define NSWindowWillStartLiveResizeNotification @"NSWindowWillStartLiveResizeNotification"
#define NSWindowDidEndLiveResizeNotification  @"NSWindowDidEndLiveResizeNotification"
#endif
#endif

extern BOOL opaqueTag;

@implementation TKApplication(TKWindowEvent)

- (void) windowActivation: (NSNotification *) notification
{
#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, _cmd, notification);
#endif
    BOOL activate = [[notification name]
	    isEqualToString:NSWindowDidBecomeKeyNotification];
    NSWindow *w = [notification object];
    TkWindow *winPtr = TkMacOSXGetTkWindow(w);

    if (winPtr && Tk_IsMapped(winPtr)) {
	GenerateActivateEvents(winPtr, activate);
    }
}

- (void) windowBoundsChanged: (NSNotification *) notification
{
#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, _cmd, notification);
#endif
    BOOL movedOnly = [[notification name]
	    isEqualToString:NSWindowDidMoveNotification];

    if (movedOnly) {
	/* constraining to screen after move not needed with AppKit */
    }

    NSWindow *w = [notification object];
    TkWindow *winPtr = TkMacOSXGetTkWindow(w);

    if (winPtr) {
	WmInfo *wmPtr = winPtr->wmInfoPtr;
	NSRect bounds = [w frame];
	int x, y, width = -1, height = -1, flags = 0;

	x = bounds.origin.x;
	y = tkMacOSXZeroScreenHeight - (bounds.origin.y + bounds.size.height);
	if (winPtr->changes.x != x || winPtr->changes.y != y){
	    flags |= TK_LOCATION_CHANGED;
	} else {
	    x = y = -1;
	}
	if (!movedOnly && (winPtr->changes.width != bounds.size.width ||
		winPtr->changes.height !=  bounds.size.height)) {
	    width = bounds.size.width - wmPtr->xInParent;
	    height = bounds.size.height - wmPtr->yInParent;
	    flags |= TK_SIZE_CHANGED;
	}
	if (Tcl_GetServiceMode() != TCL_SERVICE_NONE) {
	    /*
	     * Propagate geometry changes immediately.
	     */

	    flags |= TK_MACOSX_HANDLE_EVENT_IMMEDIATELY;
	}
	TkGenWMConfigureEvent((Tk_Window) winPtr, x, y, width, height, flags);
    }
}

- (void) windowExpanded: (NSNotification *) notification
{
#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, _cmd, notification);
#endif
    NSWindow *w = [notification object];
    TkWindow *winPtr = TkMacOSXGetTkWindow(w);

    if (winPtr) {
	winPtr->wmInfoPtr->hints.initial_state =
		TkMacOSXIsWindowZoomed(winPtr) ? ZoomState : NormalState;
	Tk_MapWindow((Tk_Window) winPtr);
	if (Tcl_GetServiceMode() != TCL_SERVICE_NONE) {
	    /*
	     * Process all Tk events generated by Tk_MapWindow().
	     */

	    while (Tcl_ServiceEvent(0)) {}
	    while (Tcl_DoOneEvent(TCL_IDLE_EVENTS|TCL_DONT_WAIT)) {}

	    /*
	     * NSWindowDidDeminiaturizeNotification is received after
	     * NSWindowDidBecomeKeyNotification, so activate manually
	     */

	    GenerateActivateEvents(winPtr, 1);
	} else {
	    Tcl_DoWhenIdle(DoWindowActivate, winPtr);
	}
    }
}

- (void) windowCollapsed: (NSNotification *) notification
{
#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, _cmd, notification);
#endif
    NSWindow *w = [notification object];
    TkWindow *winPtr = TkMacOSXGetTkWindow(w);

    if (winPtr) {
	Tk_UnmapWindow((Tk_Window) winPtr);
    }
}

- (BOOL) windowShouldClose: (NSWindow *) w
{
#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, _cmd, w);
#endif
    TkWindow *winPtr = TkMacOSXGetTkWindow(w);

    if (winPtr) {
	TkGenWMDestroyEvent((Tk_Window) winPtr);
	if (_windowWithMouse == w) {
	    _windowWithMouse = nil;
	    [w release];
	}
    }

    /*
     * If necessary, TkGenWMDestroyEvent() handles [close]ing the window,
     * so can always return NO from -windowShouldClose: for a Tk window.
     */

    return (winPtr ? NO : YES);
}

#ifdef TK_MAC_DEBUG_NOTIFICATIONS

- (void) windowDragStart: (NSNotification *) notification
{
    TKLog(@"-[%@(%p) %s] %@", [self class], self, _cmd, notification);
}

- (void) windowLiveResize: (NSNotification *) notification
{
    TKLog(@"-[%@(%p) %s] %@", [self class], self, _cmd, notification);
    //BOOL start = [[notification name] isEqualToString:NSWindowWillStartLiveResizeNotification];
}

- (void) windowMapped: (NSNotification *) notification
{
    TKLog(@"-[%@(%p) %s] %@", [self class], self, _cmd, notification);
    NSWindow *w = [notification object];
    TkWindow *winPtr = TkMacOSXGetTkWindow(w);

    if (winPtr) {
	//Tk_MapWindow((Tk_Window) winPtr);
    }
}

- (void) windowBecameVisible: (NSNotification *) notification
{
    TKLog(@"-[%@(%p) %s] %@", [self class], self, _cmd, notification);
}

- (void) windowUnmapped: (NSNotification *) notification
{
    TKLog(@"-[%@(%p) %s] %@", [self class], self, _cmd, notification);
    NSWindow *w = [notification object];
    TkWindow *winPtr = TkMacOSXGetTkWindow(w);

    if (winPtr) {
	//Tk_UnmapWindow((Tk_Window) winPtr);
    }
}
#endif /* TK_MAC_DEBUG_NOTIFICATIONS */

- (void) _setupWindowNotifications
{
    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];

#define observe(n, s) \
	[nc addObserver:self selector:@selector(s) name:(n) object:nil]
    observe(NSWindowDidBecomeKeyNotification, windowActivation:);
    observe(NSWindowDidResignKeyNotification, windowActivation:);
    observe(NSWindowDidMoveNotification, windowBoundsChanged:);
    observe(NSWindowDidResizeNotification, windowBoundsChanged:);
    observe(NSWindowDidDeminiaturizeNotification, windowExpanded:);
    observe(NSWindowDidMiniaturizeNotification, windowCollapsed:);
#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    observe(NSWindowWillMoveNotification, windowDragStart:);
    observe(NSWindowWillStartLiveResizeNotification, windowLiveResize:);
    observe(NSWindowDidEndLiveResizeNotification, windowLiveResize:);
    observe(NSWindowWillOrderOnScreenNotification, windowMapped:);
    observe(NSWindowDidOrderOnScreenNotification, windowBecameVisible:);
    observe(NSWindowDidOrderOffScreenNotification, windowUnmapped:);
#endif
#undef observe
}
@end

#pragma mark TKApplication(TKApplicationEvent)

@implementation TKApplication(TKApplicationEvent)

- (void) applicationActivate: (NSNotification *) notification
{
#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, _cmd, notification);
#endif
    [NSApp tkCheckPasteboard];
}

- (void) applicationDeactivate: (NSNotification *) notification
{
#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, _cmd, notification);
#endif
    TkSuspendClipboard();
}

- (void) applicationShowHide: (NSNotification *) notification
{
#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, _cmd, notification);
#endif
    const char *cmd = ([[notification name] isEqualToString:
	    NSApplicationDidUnhideNotification] ?
	    "::tk::mac::OnShow" : "::tk::mac::OnHide");
    Tcl_CmdInfo dummy;

    if (_eventInterp && Tcl_GetCommandInfo(_eventInterp, cmd, &dummy)) {
	int code = Tcl_EvalEx(_eventInterp, cmd, -1, TCL_EVAL_GLOBAL);

	if (code != TCL_OK) {
	    Tcl_BackgroundError(_eventInterp);
	}
	Tcl_ResetResult(_eventInterp);
    }
}

- (void) displayChanged: (NSNotification *) notification
{
#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, _cmd, notification);
#endif
    TkDisplay *dispPtr = TkGetDisplayList();

    if (dispPtr) {
	TkMacOSXDisplayChanged(dispPtr->display);
    }
}
@end

#pragma mark -

/*
 *----------------------------------------------------------------------
 *
 * GenerateUpdates --
 *
 *	Given a Macintosh update region and a Tk window this function geneates
 *	a X Expose event for the window if it is within the update region. The
 *	function will then recursivly have each damaged window generate Expose
 *	events for its child windows.
 *
 * Results:
 *	True if event(s) are generated - false otherwise.
 *
 * Side effects:
 *	Additional events may be place on the Tk event queue.
 *
 *----------------------------------------------------------------------
 */

static int
GenerateUpdates(
    HIShapeRef updateRgn,
    CGRect *updateBounds,
    TkWindow *winPtr)
{
    TkWindow *childPtr;
    XEvent event;
    CGRect bounds, damageBounds;
    HIShapeRef boundsRgn, damageRgn;

    TkMacOSXWinCGBounds(winPtr, &bounds);
    if (!CGRectIntersectsRect(bounds, *updateBounds)) {
	return 0;
    }
    if (!HIShapeIntersectsRect(updateRgn, &bounds)) {
	return 0;
    }

    /*
     * Compute the bounding box of the area that the damage occured in.
     */

    boundsRgn = HIShapeCreateWithRect(&bounds);
    damageRgn = HIShapeCreateIntersection(updateRgn, boundsRgn);
    if (HIShapeIsEmpty(damageRgn)) {
	CFRelease(damageRgn);
	CFRelease(boundsRgn);
	return 0;
    }
    HIShapeGetBounds(damageRgn, &damageBounds);

    CFRelease(damageRgn);
    CFRelease(boundsRgn);

    event.xany.serial = LastKnownRequestProcessed(Tk_Display(winPtr));
    event.xany.send_event = false;
    event.xany.window = Tk_WindowId(winPtr);
    event.xany.display = Tk_Display(winPtr);
    event.type = Expose;
    event.xexpose.x = damageBounds.origin.x - bounds.origin.x;
    event.xexpose.y = damageBounds.origin.y - bounds.origin.y;
    event.xexpose.width = damageBounds.size.width;
    event.xexpose.height = damageBounds.size.height;
    event.xexpose.count = 0;
    Tk_HandleEvent(&event);

    #ifdef TK_MAC_DEBUG_DRAWING
    NSLog(@"Expose %p {{%d, %d}, {%d, %d}}", event.xany.window, event.xexpose.x,
	event.xexpose.y, event.xexpose.width, event.xexpose.height);
    #endif

    /*
     * Generate updates for the children of this window
     */

    for (childPtr = winPtr->childList; childPtr != NULL;
	    childPtr = childPtr->nextPtr) {
	if (!Tk_IsMapped(childPtr) || Tk_IsTopLevel(childPtr)) {
	    continue;
	}
	GenerateUpdates(updateRgn, updateBounds, childPtr);
    }

    /*
     * Generate updates for any contained windows
     */

    if (Tk_IsContainer(winPtr)) {
	childPtr = TkpGetOtherWindow(winPtr);
	if (childPtr != NULL && Tk_IsMapped(childPtr)) {
	    GenerateUpdates(updateRgn, updateBounds, childPtr);
	}

	/*
	 * TODO: Here we should handle out of process embedding.
	 */
    }    

    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * GenerateActivateEvents --
 *
 *	Given a Macintosh window activate event this function generates all the
 *	X Activate events needed by Tk.
 *
 * Results:
 *	True if event(s) are generated - false otherwise.
 *
 * Side effects:
 *	Additional events may be place on the Tk event queue.
 *
 *----------------------------------------------------------------------
 */

int
GenerateActivateEvents(
    TkWindow *winPtr,
    int activeFlag)
{
    TkGenerateActivateEvents(winPtr, activeFlag);
    TkMacOSXGenerateFocusEvent(winPtr, activeFlag);
    return true;
}

/*
 *----------------------------------------------------------------------
 *
 * DoWindowActivate --
 *
 *	Idle handler that calls GenerateActivateEvents().
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Additional events may be place on the Tk event queue.
 *
 *----------------------------------------------------------------------
 */

void
DoWindowActivate(
    ClientData clientData)
{
    GenerateActivateEvents(clientData, 1);
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXGenerateFocusEvent --
 *
 *	Given a Macintosh window activate event this function generates all
 *	the X Focus events needed by Tk.
 *
 * Results:
 *	True if event(s) are generated - false otherwise.
 *
 * Side effects:
 *	Additional events may be place on the Tk event queue.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkMacOSXGenerateFocusEvent(
    TkWindow *winPtr,		/* Root X window for event. */
    int activeFlag)
{
    XEvent event;

    /*
     * Don't send focus events to windows of class help or to windows with the
     * kWindowNoActivatesAttribute.
     */

    if (winPtr->wmInfoPtr && (winPtr->wmInfoPtr->macClass == kHelpWindowClass ||
	    winPtr->wmInfoPtr->attributes & kWindowNoActivatesAttribute)) {
	return false;
    }

    /*
     * Generate FocusIn and FocusOut events. This event is only sent to the
     * toplevel window.
     */

    if (activeFlag) {
	event.xany.type = FocusIn;
    } else {
	event.xany.type = FocusOut;
    }

    event.xany.serial = LastKnownRequestProcessed(Tk_Display(winPtr));
    event.xany.send_event = False;
    event.xfocus.display = Tk_Display(winPtr);
    event.xfocus.window = winPtr->window;
    event.xfocus.mode = NotifyNormal;
    event.xfocus.detail = NotifyDetailNone;

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
    return true;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGenWMConfigureEvent --
 *
 *	Generate a ConfigureNotify event for Tk. Depending on the value of flag
 *	the values of width/height, x/y, or both may be changed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A ConfigureNotify event is sent to Tk.
 *
 *----------------------------------------------------------------------
 */

void
TkGenWMConfigureEvent(
    Tk_Window tkwin,
    int x, int y,
    int width, int height,
    int flags)
{
    XEvent event;
    WmInfo *wmPtr;
    TkWindow *winPtr = (TkWindow *) tkwin;

    if (tkwin == NULL) {
	return;
    }

    event.type = ConfigureNotify;
    event.xconfigure.serial = LastKnownRequestProcessed(Tk_Display(tkwin));
    event.xconfigure.send_event = False;
    event.xconfigure.display = Tk_Display(tkwin);
    event.xconfigure.event = Tk_WindowId(tkwin);
    event.xconfigure.window = Tk_WindowId(tkwin);
    event.xconfigure.border_width = winPtr->changes.border_width;
    event.xconfigure.override_redirect = winPtr->atts.override_redirect;
    if (winPtr->changes.stack_mode == Above) {
	event.xconfigure.above = winPtr->changes.sibling;
    } else {
	event.xconfigure.above = None;
    }

    if (!(flags & TK_LOCATION_CHANGED)) {
	x = Tk_X(tkwin);
	y = Tk_Y(tkwin);
    }
    if (!(flags & TK_SIZE_CHANGED)) {
	width = Tk_Width(tkwin);
	height = Tk_Height(tkwin);
    }
    event.xconfigure.x = x;
    event.xconfigure.y = y;
    event.xconfigure.width = width;
    event.xconfigure.height = height;

    if (flags & TK_MACOSX_HANDLE_EVENT_IMMEDIATELY) {
	Tk_HandleEvent(&event);
    } else {
	Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
    }

    /*
     * Update window manager information.
     */

    if (Tk_IsTopLevel(winPtr)) {
	wmPtr = winPtr->wmInfoPtr;
	if (flags & TK_LOCATION_CHANGED) {
	    wmPtr->x = x;
	    wmPtr->y = y;
	    wmPtr->flags &= ~(WM_NEGATIVE_X | WM_NEGATIVE_Y);
	}
	if ((flags & TK_SIZE_CHANGED) && !(wmPtr->flags & WM_SYNC_PENDING) &&
		((width != Tk_Width(tkwin)) || (height != Tk_Height(tkwin)))) {
	    if ((wmPtr->width == -1) && (width == winPtr->reqWidth)) {
		/*
		 * Don't set external width, since the user didn't change it
		 * from what the widgets asked for.
		 */
	    } else if (wmPtr->gridWin != NULL) {
		wmPtr->width = wmPtr->reqGridWidth
			+ (width - winPtr->reqWidth)/wmPtr->widthInc;
		if (wmPtr->width < 0) {
		    wmPtr->width = 0;
		}
	    } else {
		wmPtr->width = width;
	    }

	    if ((wmPtr->height == -1) && (height == winPtr->reqHeight)) {
		/*
		 * Don't set external height, since the user didn't change it
		 * from what the widgets asked for.
		 */
	    } else if (wmPtr->gridWin != NULL) {
		wmPtr->height = wmPtr->reqGridHeight
			+ (height - winPtr->reqHeight)/wmPtr->heightInc;
		if (wmPtr->height < 0) {
		    wmPtr->height = 0;
		}
	    } else {
		wmPtr->height = height;
	    }

	    wmPtr->configWidth = width;
	    wmPtr->configHeight = height;
	}
    }

    /*
     * Now set up the changes structure. Under X we wait for the
     * ConfigureNotify to set these values. On the Mac we know imediatly that
     * this is what we want - so we just set them. However, we need to make
     * sure the windows clipping region is marked invalid so the change is
     * visible to the subwindow.
     */

    winPtr->changes.x = x;
    winPtr->changes.y = y;
    winPtr->changes.width = width;
    winPtr->changes.height = height;
    TkMacOSXInvalClipRgns(tkwin);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGenWMDestroyEvent --
 *
 *	Generate a WM Destroy event for Tk.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A WM_PROTOCOL/WM_DELETE_WINDOW event is sent to Tk.
 *
 *----------------------------------------------------------------------
 */

void
TkGenWMDestroyEvent(
    Tk_Window tkwin)
{
    XEvent event;

    event.xany.serial = LastKnownRequestProcessed(Tk_Display(tkwin));
    event.xany.send_event = False;
    event.xany.display = Tk_Display(tkwin);

    event.xclient.window = Tk_WindowId(tkwin);
    event.xclient.type = ClientMessage;
    event.xclient.message_type = Tk_InternAtom(tkwin, "WM_PROTOCOLS");
    event.xclient.format = 32;
    event.xclient.data.l[0] = Tk_InternAtom(tkwin, "WM_DELETE_WINDOW");
    Tk_HandleEvent(&event);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmProtocolEventProc --
 *
 *	This procedure is called by the Tk_HandleEvent whenever a ClientMessage
 *	event arrives whose type is "WM_PROTOCOLS". This procedure handles the
 *	message from the window manager in an appropriate fashion.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Depends on what sort of handler, if any, was set up for the protocol.
 *
 *----------------------------------------------------------------------
 */

void
TkWmProtocolEventProc(
    TkWindow *winPtr,		/* Window to which the event was sent. */
    XEvent *eventPtr)		/* X event. */
{
    WmInfo *wmPtr;
    ProtocolHandler *protPtr;
    Tcl_Interp *interp;
    Atom protocol;
    int result;

    wmPtr = winPtr->wmInfoPtr;
    if (wmPtr == NULL) {
	return;
    }
    protocol = (Atom) eventPtr->xclient.data.l[0];
    for (protPtr = wmPtr->protPtr; protPtr != NULL;
	    protPtr = protPtr->nextPtr) {
	if (protocol == protPtr->protocol) {
	    Tcl_Preserve(protPtr);
	    interp = protPtr->interp;
	    Tcl_Preserve(interp);
	    result = Tcl_EvalEx(interp, protPtr->command, -1, TCL_EVAL_GLOBAL);
	    if (result != TCL_OK) {
		Tcl_AddErrorInfo(interp, "\n    (command for \"");
		Tcl_AddErrorInfo(interp,
			Tk_GetAtomName((Tk_Window) winPtr, protocol));
		Tcl_AddErrorInfo(interp, "\" window manager protocol)");
		Tcl_BackgroundError(interp);
	    }
	    Tcl_Release(interp);
	    Tcl_Release(protPtr);
	    return;
	}
    }

    /*
     * No handler was present for this protocol. If this is a WM_DELETE_WINDOW
     * message then just destroy the window.
     */

    if (protocol == Tk_InternAtom((Tk_Window) winPtr, "WM_DELETE_WINDOW")) {
	Tk_DestroyWindow((Tk_Window) winPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_MacOSXIsAppInFront --
 *
 *	Returns 1 if this app is the foreground app.
 *
 * Results:
 *	1 if app is in front, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Tk_MacOSXIsAppInFront(void)
{
    Boolean isFrontProcess = true;
#if MAC_OS_X_VERSION_MIN_REQUIRED < 1060
    ProcessSerialNumber frontPsn, ourPsn = {0, kCurrentProcess};

    if (noErr == GetFrontProcess(&frontPsn)){
	SameProcess(&frontPsn, &ourPsn, &isFrontProcess);
    }
#else
    isFrontProcess = [NSRunningApplication currentApplication].active;
#endif
    return (isFrontProcess == true);
}

#pragma mark TKContentView

#import <ApplicationServices/ApplicationServices.h>

/*
 * Custom content view for use in Tk NSWindows.
 * 
 * Since Tk handles all drawing of widgets, we only use the AppKit event loop
 * as a source of input events.  To do this, we overload the NSView drawRect
 * method with a method which generates Expose events for Tk but does no
 * drawing.  The redrawing operations are then done when Tk processes these
 * events.
 *
 * Earlier versions of Mac Tk used subclasses of NSView, e.g. NSButton, as the
 * basis for Tk widgets.  These would then appear as subviews of the
 * TKContentView.  To prevent the AppKit from redrawing and corrupting the Tk
 * Widgets it was necessary to use Apple private API calls.  In order to avoid
 * using private API calls, the NSView-based widgets have been replaced with
 * normal Tk widgets which draw themselves as native widgets by using the
 * HITheme API.
 *
 */

/*Restrict event processing to Expose events.*/
static Tk_RestrictAction
ExposeRestrictProc(
    ClientData arg,
    XEvent *eventPtr)
{
    return (eventPtr->type==Expose && eventPtr->xany.serial==PTR2UINT(arg)
	    ? TK_PROCESS_EVENT : TK_DEFER_EVENT);
}

/*Restrict event processing to ConfigureNotify events.*/
static Tk_RestrictAction
ConfigureRestrictProc(
    ClientData arg,
    XEvent *eventPtr)
{
    return (eventPtr->type==ConfigureNotify ? TK_PROCESS_EVENT : TK_DEFER_EVENT);
}

@implementation TKContentView(TKWindowEvent)

- (void) drawRect: (NSRect) rect
{
    const NSRect *rectsBeingDrawn;
    NSInteger rectsBeingDrawnCount;
    
    [self getRectsBeingDrawn:&rectsBeingDrawn count:&rectsBeingDrawnCount];

#ifdef TK_MAC_DEBUG_DRAWING
    TKLog(@"-[%@(%p) %s%@]", [self class], self, _cmd, NSStringFromRect(rect));
    [[NSColor colorWithDeviceRed:0.0 green:1.0 blue:0.0 alpha:.1] setFill];
    NSRectFillListUsingOperation(rectsBeingDrawn, rectsBeingDrawnCount,
	    NSCompositeSourceOver);
#endif

 	    
    CGFloat height = [self bounds].size.height;
    HIMutableShapeRef drawShape = HIShapeCreateMutable();

    while (rectsBeingDrawnCount--) {
	CGRect r = NSRectToCGRect(*rectsBeingDrawn++);
	r.origin.y = height - (r.origin.y + r.size.height);
	HIShapeUnionWithRect(drawShape, &r);
    }
    if (CFRunLoopGetMain() == CFRunLoopGetCurrent()) {
	[self generateExposeEvents:(HIShapeRef)drawShape];
    } else {
	[self performSelectorOnMainThread:@selector(generateExposeEvents:)
		withObject:(id)drawShape waitUntilDone:NO
		modes:[NSArray arrayWithObjects:NSRunLoopCommonModes,

			NSEventTrackingRunLoopMode, NSModalPanelRunLoopMode,
			nil]];
    }
   
    CFRelease(drawShape);
  
}

-(void) setFrameSize: (NSSize)newsize
{
    [super setFrameSize: newsize];
    if ([self inLiveResize]) {
	NSWindow *w = [self window];
	TkWindow *winPtr = TkMacOSXGetTkWindow(w);
	Tk_Window tkwin = (Tk_Window) winPtr;
	unsigned int width = (unsigned int)newsize.width;
	unsigned int height=(unsigned int)newsize.height;
	ClientData oldArg;
    	Tk_RestrictProc *oldProc;

	/* This can be called from outside the Tk event loop.
	 * Since it calls Tcl_DoOneEvent, we need to make sure we
	 * don't clobber the AutoreleasePool set up by the caller.
	 */
	[NSApp setPoolProtected:YES];
	
	/*
	 * Try to prevent flickers and flashes.
	 */
	[w disableFlushWindow];
	NSDisableScreenUpdates();
	
	/* Disable Tk drawing until the window has been completely configured.*/
	TkMacOSXSetDrawingEnabled(winPtr, 0);

	 /* Generate and handle a ConfigureNotify event for the new size.*/
	TkGenWMConfigureEvent(tkwin, Tk_X(tkwin), Tk_Y(tkwin), width, height,
			      TK_SIZE_CHANGED | TK_MACOSX_HANDLE_EVENT_IMMEDIATELY);
    	oldProc = Tk_RestrictEvents(ConfigureRestrictProc, NULL, &oldArg);
	while (Tk_DoOneEvent(TK_X_EVENTS|TK_DONT_WAIT)) {}
    	Tk_RestrictEvents(oldProc, oldArg, &oldArg);

	/* Now that Tk has configured all subwindows we can create the clip regions. */
	TkMacOSXSetDrawingEnabled(winPtr, 1);
	TkMacOSXInvalClipRgns(tkwin);
	TkMacOSXUpdateClipRgn(winPtr);

	 /* Finally, generate and process expose events to redraw the window. */
	HIRect bounds = NSRectToCGRect([self bounds]);
	HIShapeRef shape = HIShapeCreateWithRect(&bounds);
	[self generateExposeEvents: shape];
	while (Tk_DoOneEvent(TK_ALL_EVENTS|TK_DONT_WAIT)) {}
	[w enableFlushWindow];
	[w flushWindowIfNeeded];
	NSEnableScreenUpdates();
	[NSApp setPoolProtected:NO];
    }
}

/*
 * As insurance against bugs that might cause layout glitches during a live
 * resize, we redraw the window one more time at the end of the resize
 * operation.
 */

- (void)viewDidEndLiveResize
{
    HIRect bounds = NSRectToCGRect([self bounds]);
    HIShapeRef shape = HIShapeCreateWithRect(&bounds);
    [super viewDidEndLiveResize];
    [self generateExposeEvents: shape];

}

/* Core method of this class: generates expose events for redrawing.
 * Whereas drawRect is intended to be called only from the Appkit event
 * loop, this can be called from Tk.  If the Tcl_ServiceMode is set to
 * TCL_SERVICE_ALL then the expose events will be immediately removed
 * from the Tcl event loop and processed.  Typically, they should be queued,
 * however.
 */
- (void) generateExposeEvents: (HIShapeRef) shape
{
    [self generateExposeEvents:shape childrenOnly:0];
}

- (void) generateExposeEvents: (HIShapeRef) shape
		 childrenOnly: (int) childrenOnly
{
    TkWindow *winPtr = TkMacOSXGetTkWindow([self window]);
    unsigned long serial;
    CGRect updateBounds;
    int updatesNeeded;

    if (!winPtr) {
		return;
    }

    /* Generate Tk Expose events. */
    HIShapeGetBounds(shape, &updateBounds);
    /* All of these events will share the same serial number. */
    serial = LastKnownRequestProcessed(Tk_Display(winPtr));
    updatesNeeded = GenerateUpdates(shape, &updateBounds, winPtr);

    /* Process the Expose events if the service mode is TCL_SERVICE_ALL */
    if (updatesNeeded && Tcl_GetServiceMode() == TCL_SERVICE_ALL) {
	ClientData oldArg;
    	Tk_RestrictProc *oldProc = Tk_RestrictEvents(ExposeRestrictProc,
						     UINT2PTR(serial), &oldArg);
    	while (Tcl_ServiceEvent(TCL_WINDOW_EVENTS)) {}
    	Tk_RestrictEvents(oldProc, oldArg, &oldArg);
    }
}

/*
 * This is no-op on 10.7 and up because Apple has removed this widget,
 * but we are leaving it here for backwards compatibility.
 */
- (void) tkToolbarButton: (id) sender
{
#ifdef TK_MAC_DEBUG_EVENTS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, _cmd);
#endif
    XVirtualEvent event;
    int x, y;
    TkWindow *winPtr = TkMacOSXGetTkWindow([self window]);
    Tk_Window tkwin = (Tk_Window) winPtr;
    bzero(&event, sizeof(XVirtualEvent));
    event.type = VirtualEvent;
    event.serial = LastKnownRequestProcessed(Tk_Display(tkwin));
    event.send_event = false;
    event.display = Tk_Display(tkwin);
    event.event = Tk_WindowId(tkwin);
    event.root = XRootWindow(Tk_Display(tkwin), 0);
    event.subwindow = None;
    event.time = TkpGetMS();
    XQueryPointer(NULL, winPtr->window, NULL, NULL,
	    &event.x_root, &event.y_root, &x, &y, &event.state);
    Tk_TopCoordsToWindow(tkwin, x, y, &event.x, &event.y);
    event.same_screen = true;
    event.name = Tk_GetUid("ToolbarButton");
    Tk_QueueWindowEvent((XEvent *) &event, TCL_QUEUE_TAIL);
}

- (BOOL) isOpaque
{
    NSWindow *w = [self window];

    if (opaqueTag) {
      return YES;
	} else {

     return (w && (([w styleMask] & NSTexturedBackgroundWindowMask) ||
    	    ![w isOpaque]) ? NO : YES);
    }
}

- (BOOL) wantsDefaultClipping
{
    return NO;
}

- (BOOL) acceptsFirstResponder
{
    return YES;
}

- (void) keyDown: (NSEvent *) theEvent
{
#ifdef TK_MAC_DEBUG_EVENTS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, _cmd, theEvent);
#endif
}

@end

/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
