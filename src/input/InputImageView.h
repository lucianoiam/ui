// InputImageView.h
// Objective-C subclass used for capturing mouse / input and forwarding to JS.
// Guarded so non-ObjC compilation units see only an opaque forward decl.
#pragma once

#ifdef __OBJC__
 #import <Cocoa/Cocoa.h>
 @interface InputImageView : NSImageView
 @end
#else
 typedef struct objc_object InputImageView; // opaque
#endif
