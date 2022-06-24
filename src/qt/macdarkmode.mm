// Copyright (c) 2020 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "macdarkmode.h"

#include <AppKit/AppKit.h>
#include <objc/runtime.h>

bool isDarkMode()
{
    NSString *interfaceStyle = [NSUserDefaults.standardUserDefaults valueForKey:@"AppleInterfaceStyle"];
    return [interfaceStyle isEqualToString:@"Dark"];
}


// This is a work around for our not having as yet any "Dark Mode" compatible
// stylesheets.  Enforce the use of known-good Aqua style on systems that support
// the NSAppearance property.
//
// That is to say, the interface is only available on 10.14+.
//@interface NSApplication (NSAppearanceCustomization) <NSAppearanceCustomization>
//#pragma clang diagnostic push
//#pragma clang diagnostic ignored "-Wavailability"
//@property (nullable, strong) NSAppearance *appearance;
//@property (readonly, strong) NSAppearance *effectiveAppearance;
//#pragma clang diagnostic pop
//@end

//void disableDarkMode()
//{
//    if ([[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:(NSOperatingSystemVersion){10,14,0}])
//    {
//        NSApp.appearance = [NSAppearance appearanceNamed: NSAppearanceNameAqua];
//    }
//}

