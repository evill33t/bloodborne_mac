// SPDX-License-Identifier: GPL-2.0-or-later
#import <Foundation/Foundation.h>
extern "C" int bb_guest_main(int,char**);
extern "C" int bbmetal_window_main(int,char**,int (*)(int,char**));
int main(int argc,char** argv) {
    @autoreleasepool { return bbmetal_window_main(argc,argv,bb_guest_main); }
}
