
#import "AppDelegate.h"
#include "png_suite_test.h"
#include "jpg_test.h"
#include "csv_test.h"
#include "mo_test.h"
#include "wav_test.h"

@interface AppDelegate ()

@end

@implementation AppDelegate

@synthesize window;

- (BOOL)application:(__unused UIApplication *)application didFinishLaunchingWithOptions:(__unused NSDictionary *)launchOptions {
    self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    self.window.rootViewController = [UIViewController new];
    [self.window makeKeyAndVisible];
    
    const char *path = [[NSBundle mainBundle].bundlePath UTF8String];

    bool verbose = false;
    png_suite_test(path, path, verbose);
    wav_test(path, verbose);
    jpg_test(path, path, verbose);
    csv_test(path, verbose);
    gettext_test(path, verbose);

#if 0
    char *src_path = get_full_path(path, "crash0", "png");
    FILE *file = fopen(src_path, "rb");
    if (!file) {
        printf("Test file not found.\n");
    } else {
        ok_png *result = ok_png_read(file, OK_PNG_COLOR_FORMAT_RGBA);
        fclose(file);
        if (result->error_message) {
            printf("%s\n", result->error_message);
        }
        ok_png_free(result);
    }
    free(src_path);
#endif

#if 0
    char *in_filename = get_full_path(path, "2001-stargate", "jpg");
    FILE *file = fopen(in_filename, "rb");
    if (file) {
        ok_jpg *jpg = ok_jpg_read(file, OK_JPG_COLOR_FORMAT_BGRA);
        if (jpg->data) {
            CGDataProviderRef dataProvider = CGDataProviderCreateWithData(NULL, jpg->data, jpg->width * jpg->height * 4, NULL);

            NSUInteger bytesPerRow = 4 * jpg->width;
            CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
            CGBitmapInfo bitmapInfo = (CGBitmapInfo)(kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst);
            CGImageRef imageRef = CGImageCreate(jpg->width, jpg->height, 8, 32, bytesPerRow, colorSpace, bitmapInfo, dataProvider, NULL, NO, kCGRenderingIntentDefault);
            UIImage *image = [UIImage imageWithCGImage:imageRef scale:2.0 orientation:UIImageOrientationUp];
            CGColorSpaceRelease(colorSpace);
            CGDataProviderRelease(dataProvider);

            UIImageView *imageView = [[UIImageView alloc] initWithImage:image];
            imageView.backgroundColor = [UIColor grayColor];
            [self.window.rootViewController.view addSubview:imageView];
        }
        ok_jpg_free(jpg);
        fclose(file);
    }
    free(in_filename);
#endif

    return YES;
}

- (void)applicationWillResignActive:(__unused UIApplication *)application {
    // Sent when the application is about to move from active to inactive state. This can occur for certain types of temporary interruptions (such as an incoming phone call or SMS message) or when the user quits the application and it begins the transition to the background state.
    // Use this method to pause ongoing tasks, disable timers, and throttle down OpenGL ES frame rates. Games should use this method to pause the game.
}

- (void)applicationDidEnterBackground:(__unused UIApplication *)application {
    // Use this method to release shared resources, save user data, invalidate timers, and store enough application state information to restore your application to its current state in case it is terminated later.
    // If your application supports background execution, this method is called instead of applicationWillTerminate: when the user quits.
}

- (void)applicationWillEnterForeground:(__unused UIApplication *)application {
    // Called as part of the transition from the background to the inactive state; here you can undo many of the changes made on entering the background.
}

- (void)applicationDidBecomeActive:(__unused UIApplication *)application {
    // Restart any tasks that were paused (or not yet started) while the application was inactive. If the application was previously in the background, optionally refresh the user interface.
}

- (void)applicationWillTerminate:(__unused UIApplication *)application {
    // Called when the application is about to terminate. Save data if appropriate. See also applicationDidEnterBackground:.
}

@end
