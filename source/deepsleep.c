/* Deep Sleep for Mac OS 10.4.3 and above */
/* M. Beaumel (cochonou@fastmail.fm), 09/08/10 */
#define VERSION "1.3 (04/11/12)"

/*
 * Copyright (C) 2010 M. Beaumel
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
 
#include <IOKit/IOKitLib.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
/* #include <IOKit/pwr_mgt/IOPMLibPrivate.h> */
#include "IOPMLibPrivate.h"
#include <IOKit/ps/IOPowerSources.h>
/* #include <IOKit/ps/IOPowerSourcesPrivate.h> */
#include "IOPowerSourcesPrivate.h"
#include <IOKit/IOMessage.h>
#include <IOKit/IOReturn.h>

#include <CoreFoundation/CFNumber.h>
#include <CoreFoundation/CFDictionary.h>
#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SystemConfiguration.h>
/* #include <SystemConfiguration/SCValidation.h> */
#include "SCValidation.h"

#include <mach/mach_init.h> 
#include <mach/mach_port.h>
#include <mach/mach_interface.h>

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/stat.h>

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

io_connect_t           root_power_port;
IONotificationPortRef  notifyPortRef;	
SCDynamicStoreRef      ds = 0;
CFDictionaryRef        live_settings = 0;
io_object_t            notifierObject;
CFNumberType           hm_type;
CFNumberType           profile_type;
CFNumberRef            hm_ref = 0;
CFNumberRef            profile_ref = 0;
int                    debug = 0;
CFTypeRef              ps_info = 0;
CFStringRef            current_ps = 0;
CFDictionaryRef        active_prof = 0;
int                    mute = 0;


/* Callback function to power notifications */
void PowerCallBack(void* refCon, io_service_t service, natural_t messageType, void * messageArgument)
{
	/* printf( "messageType %08lx, arg %08lx\n",
            (long unsigned int)messageType,
            (long unsigned int)messageArgument ); */
			
	switch (messageType) {
		case kIOMessageSystemHasPoweredOn:                                                                                     /* System is waking up */
			CFRunLoopStop(CFRunLoopGetCurrent());                                                                                 /* Stop the loop in main() */
			break;
		case kIOMessageSystemWillSleep:                                                                                        /* System is going to sleep */
			if (!mute) printf("System is going down to sleep...\n");
			IOAllowPowerChange(root_power_port, (long)messageArgument);                                                           /* Allow the sleep operation */
			break;
	}                                                                         
}

/* Cleanup of the created core fundation objects */
void CFCleanup()
{
	if (live_settings) CFRelease(live_settings);
	if (ds) CFRelease(ds);	
	if (ps_info) CFRelease(ps_info);
	if (active_prof) CFRelease(active_prof);
	if (hm_ref) CFRelease(hm_ref);
	if (profile_ref) CFRelease(profile_ref);
	if (current_ps) CFRelease(current_ps);
}

/* Cleanup of the open ports */
void PortsCleanup()
{
	CFRunLoopRemoveSource(CFRunLoopGetCurrent(), IONotificationPortGetRunLoopSource(notifyPortRef), kCFRunLoopCommonModes);    /* Remove the notification port from the runloop */
	IODeregisterForSystemPower(&notifierObject);                                                                               /* Deregister from power notifications */
	IOServiceClose(root_power_port);                                                                                           /* Close the Root Power Domain IOService port */
	IONotificationPortDestroy(notifyPortRef);                                                                                  /* Destroy the notification port */
}

int SetHibernateMode(int mode, CFStringRef ps) {
	CFNumberRef            target_hm;
	CFDictionaryRef        tmp_settings = 0;
	CFMutableDictionaryRef settings = 0;
	CFDictionaryRef        tmp_node = 0;
    CFMutableDictionaryRef node = 0;
	IOReturn               ret;
	int                    result = 0;
	
	target_hm = CFNumberCreate(kCFAllocatorDefault, hm_type, &mode);                                                       /* Create a CFNumber with the mode value */
	tmp_settings = IOPMCopyPMPreferences();                                                                                /* Get the power management preferences */
	if(!tmp_settings) {                                                                                                       /* On failure, quit */
		CFRelease(tmp_settings);
		CFRelease(target_hm);
		return 1;
	}
	settings = CFDictionaryCreateMutableCopy(kCFAllocatorDefault, 0, tmp_settings);                                        /* Copy the preferences to a mutable dictionary */
	CFRelease(tmp_settings);

	tmp_node = isA_CFDictionary(CFDictionaryGetValue(settings, ps));								                       /* Get the power source dictionary */
    if(tmp_node) {
        node = CFDictionaryCreateMutableCopy(0, 0, tmp_node);                                                              /* On success, copy it to a mutable dictionary */
        if(node) {
			CFDictionarySetValue(node, CFSTR("Hibernate Mode"), target_hm);                                                   /* Set the hibernate mode to its new value */
            CFDictionarySetValue(settings, ps, node);                                                                         /* link the new power source dictionary to the pm preferences*/
            CFRelease(node);
        }
    }
	
	if(kIOReturnSuccess != (ret = IOPMSetPMPreferences(settings))) {                                                       /* Set the new pm preferences */
		if(ret == kIOReturnNotPrivileged) {                                                                                   /* On failure, quit */
			printf("deepsleep must be run as root...\n");
		} else {
			printf("Error 0x%08x writing Energy Saver preferences to disk\n", ret);
		}
	result = 1;
	}
	
	CFRelease(settings);                                                                                                  /* Cleanup */
	CFRelease(target_hm);
	return result;
}

/* Set the active power profile */
int SetActiveProfile(int nb, CFStringRef ps, CFDictionaryRef profile) {
	CFNumberRef            target_nb;
	CFMutableDictionaryRef custom_profile;
	IOReturn               ret;
	int                    result = 0;
	
	target_nb = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &nb);                                              /* Create a CFNumber with the active profile value */
	custom_profile = CFDictionaryCreateMutableCopy(0, 0, profile);                                                       /* Copy the preferences to a mutable dictionary */
	CFDictionarySetValue(custom_profile, ps, target_nb);                                                                 /* Set the active profile to its new value */
	if(kIOReturnSuccess != (ret = IOPMSetActivePowerProfiles(custom_profile))) {                                         /* Set the new profile */
		printf("Error 0x%08x writing Energy Saver preferences to disk\n", ret);                                             /* On failure, quit */
		result = 1;
	}
	
	CFRelease(custom_profile);
	CFRelease(target_nb);
	return result;
}

/* Main program */
int main (int argc, const char * argv[])
{
	enum                   suspend_type {soft, dump, hard};
	int                    vmmib[2] = {CTL_VM, VM_SWAPUSAGE};
	int					   osmib[2] = {CTL_KERN, KERN_OSRELEASE};
	int                    original_mode;
	int                    target_mode;
	int                    default_mode;
	int                    original_profile;
	int                    target_profile = -1;
	void                   *refCon;
	struct xsw_usage       swap;
	size_t                 swlen = sizeof(swap);
	size_t				   oslen;
	char                   *kernel_version;
	int                    major_version = 0;
	int                    minor_version = 0;
	struct stat            sleepimage;
	                                                                                                                               /* By default, */
	int                    do_sleep = 1;                                                                                           /* send the sleep call, */
	int                    repair = 0;                                                                                             /* do not check the validity of the original hibernation mode, */
	int                    check_hibernation = 1;                                                                                  /* check if the hibernation file is present, */
	int                    check_os = 1;                                                                                           /* check if the operating system is supported, */
	enum suspend_type      target_suspend = soft;                                                                                  /* send computer to software suspend mode, */
	int                    restore = 1;                                                                                            /* restore the original mode, */
    int os_ml = 0; // mountain lion

	if (argc >= 2) {
		if (!strcmp(argv[1], "-v")) {                                                                                              /* Display version number if invoked with -v */
			printf("deepsleep build %s\n", VERSION);
			return 0;
		} else if (!strcmp(argv[1], "-h")) {
			printf("deepsleep usage: deepsleep [-bdhrvsu] [hard|dump|soft]\n");
			printf("                 -b : bypass the hibernation file check\n");
			printf("                 -d : debug mode - be verbose\n");
			printf("                 -h : display this help screen\n");
			printf("                 -m : mute - be silent\n");
			printf("                 -o : do not restore the original hibernation mode\n");
			printf("                 -r : repair the default hibernation mode if needed\n");
			printf("                 -s : simulation - do not send the computer to sleep\n");
			printf("                 -v : display version number\n");
			printf("                 -u : perform operations even on unsupported OS revisions\n");
			printf("                 hard : send computer to hardware suspend mode\n");
			printf("                 dump : send computer to safe hardware suspend mode\n");
			printf("                 soft : send computer to software suspend mode (by default)\n");
			return 0;
		} else {
			if (argc >= 3) {
				if (strstr(argv[1], "b"))                                                                                             /* Do not check the existence of the hibernation file if invoked with -b */
					check_hibernation = 0;	
				if (strstr(argv[1], "d"))                                                                                             /* Print debug information if invoked with -d */
					debug = 1;
				if (strstr(argv[1], "o"))                                                                                             /* Do not restore the original hibernation mode if invoked with -o */
					restore = 0;
				if (strstr(argv[1], "r"))                                                                                             /* Check for the validity of the original hibernation mode if invoked with -r*/ 
					repair = 1;
				if (strstr(argv[1], "s"))                                                                                             /* Do not send the sleep call if invoked with -s */
					do_sleep = 0;
				if (strstr(argv[1], "u"))                                                                                             /* Do not care about OS revision if invoked with -u */
					check_os = 0;
				if (strstr(argv[1], "m"))
					mute = 1;
			} 	
			if (strstr(argv[argc-1], "hard"))                                                                                         /* Send computer to hardware suspend mode instead of software suspend mode if the hard argument is present */
				target_suspend = hard;
			else if (strstr(argv[argc-1], "dump"))                                                                                    /* Send computer to safe hardware suspend mode instead of software suspend mode if the safe argument is present */
				target_suspend = dump;	
		}
	}
	
	if (sysctl(osmib, 2, NULL, &oslen, NULL, 0) == -1) {                                                                          /* Get the operating system revision length */
		printf("Failed to get the operating system revision\n");                                                                     /* On failure: quit */
		return 1;
	} else {
		kernel_version = malloc(oslen * sizeof(char));                                        
		sysctl(osmib, 2, kernel_version, &oslen, NULL, 0);                                                                       /* Get the operating system revision length */
		sscanf(kernel_version, "%d.%d", &major_version, &minor_version);
		free(kernel_version);
	}
	if (debug) {
		printf("OS revision: %d.%d", major_version, minor_version);
		if (!check_os) printf(" (ignored)");
		printf("\n");
	}

	if (major_version >= 12) {
        os_ml = 1;
    } else if (check_os && (major_version != 8 || minor_version < 3) && (major_version <= 8)) {                                         /* If needed, check if major version is 8 (Mac OS X 10.4) and minor version is greater or equal than 3. Mac OS X 10.5 is also supported.*/
		printf("This operating system is not supported\n");                                                                           /* On failure: quit */
		return 1;
	}
	
	if (check_hibernation && stat("/private/var/vm/sleepimage", &sleepimage)) {                                                  /* If needed, check if the hibernation file (/private/var/vm/sleepimage) exists */
		printf("Hibernation file is missing\n");                                                                                     /* On failure: quit */   
		return 1;
	}
	
    if (!os_ml) {
        if (sysctl(vmmib, 2, &swap, &swlen, NULL, 0) == -1){                                                                       /* Get the current virtual memory parameters */
            printf("Failed to get the virtual memory information\n");                                                                  /* On failure: quit */
            return 1;
        } else {
            if (swap.xsu_encrypted) {                                                                                                  /* Check if the virtual memory is encrypted */
                default_mode = 7;
                if (target_suspend == dump) {
                    target_mode = default_mode;                                                                                               /* If so, we will sleep with hibernate mode 7 for safe hardware suspend */
                } else /*if (target_suspend == soft)*/ {
                    target_mode = 5;                                                                                                          /* or with hibernate mode 5 for software suspend */
                }
                if (debug) printf("VM is encrypted\n");
            }
            else {
                default_mode = 3;
                if (target_suspend == dump) {
                    target_mode = default_mode;                                                                                               /* else, we will use the regular mode 3 for safe hardware suspend */
                } else /*if (target_suspend == soft)*/ {
                    target_mode = 1;                                                                                                          /* or the regular mode 1 for software suspend */
                }
                if (debug) printf("VM is not encrypted\n");
            }
            if (target_suspend == hard)                                                                                                 /* If we only want to perform basic hardware suspend */
                target_mode = 0;                                                                                                            /* we will sleep with hibernate mode 0 */
        }
    } else {
        // OS X 10.8
        default_mode = 3;
        if ( target_suspend == dump) {
            target_mode = default_mode;
        } else if (target_suspend == hard) {
            target_mode = 0;
        } else {
            target_mode = 25;
        }
    }

    if (debug) printf("target mode: %d\n", target_mode);
    
	ps_info = IOPSCopyPowerSourcesInfo();                                                                                       /* Get the power source information */
	if (ps_info) {
		current_ps = IOPSGetProvidingPowerSourceType(ps_info);                                                                     /* On success, store the active power source */
	} else {
		printf("Failed to get the power source information\n");                                                                    /* On failure: quit */
		return 1;
	}
	if (debug) printf("target power source: %s\n", CFStringGetCStringPtr(current_ps, kCFStringEncodingMacRoman));	
	
	active_prof = IOPMCopyActivePowerProfiles();                                                                                /* Get the power profiles */
    if (!active_prof) {
        printf("Failed to get the active profile\n");
		CFCleanup();
		return 1;
    }
	if (CFDictionaryContainsKey(active_prof, current_ps)) {                                                                     /* Get the active profile corresponding to the current power source */
		profile_ref = (CFNumberRef) CFDictionaryGetValue(active_prof, current_ps);
		profile_type = CFNumberGetType(profile_ref);
		CFNumberGetValue(profile_ref, profile_type, &original_profile);                                                            /* On succes, store its value */
		if (debug) printf("original profile: %d\n", original_profile);
	} else {
		printf("Failed to get the power management settings\n");                                                                   /* On failure: quit */
		CFCleanup();
		return 1;
	}	
		
	ds = SCDynamicStoreCreate(NULL, CFSTR("deepsleep"), NULL, NULL);                                                            /* Create a new dynamic store */
	live_settings = SCDynamicStoreCopyValue(ds, CFSTR(kIOPMDynamicStoreSettingsKey));                                           /* Read current settings */
	if(!isA_CFDictionary(live_settings)) {                                                                                         /* We did not get the settings: quit */
		printf("Failed to get the power management settings\n");
		CFCleanup();
		return 1;                                               
	}
	
	if (CFDictionaryContainsKey(live_settings, CFSTR("Hibernate Mode"))) {                                                      /* Check if the hibernate mode key exists */
		hm_ref = (CFNumberRef) CFDictionaryGetValue(live_settings, CFSTR("Hibernate Mode"));                                       /* On success, get its value */
		hm_type = CFNumberGetType(hm_ref);
		CFNumberGetValue(hm_ref, hm_type, &original_mode);
		if (debug) printf("original mode: %d\n", original_mode);
	}
	else {                                                                                                                         /* On failure, cleanup and quit */ 
		printf("Failed to get the hibernation mode\n");
		CFCleanup();                                                                                                  
		return 1;
	}
	
	if (repair && original_mode == target_mode) {                                                                              /* If the original mode is the same as the target mode */
		original_mode = default_mode;                                                                                              /* A crash has probably happened during hibernation: we will set back the hibernation mode to its default value after wakeup */ 
		if (debug) printf("repair mode to: %d\n", default_mode);
	}
	
	root_power_port = IORegisterForSystemPower(refCon, &notifyPortRef, PowerCallBack, &notifierObject);                         /* Register to the Root Power Domain IOService: notifications will be handled by the PowerCallBack functions */
	if (!root_power_port) {                                                                                                        /* Registering failed: quit */
		printf("Failed to register to the Root Power Domain IOService\n");		
		CFCleanup();
		return 1;
	}
	
	CFRunLoopAddSource(CFRunLoopGetCurrent(), IONotificationPortGetRunLoopSource(notifyPortRef), kCFRunLoopCommonModes);        /* Add the notification port to the run loop */
	
	if (SetActiveProfile(target_profile, current_ps, active_prof)) {                                                            /* Set the active power profile to custom (-1) */
		printf("Failed to set the active profile\n");
		PortsCleanup();
		CFCleanup();
		return 1;
	}
	if (SetHibernateMode(target_mode, current_ps))	{                                                                           /* Set the hibernate mode to target mode */
		printf("Failed to set the hibernation mode\n");
		SetActiveProfile(original_profile, current_ps, active_prof);
		PortsCleanup();
		CFCleanup();
		return 1;
	}
	
	if (do_sleep) {                                                                                                             /* If we are not in simulation mode */
		sleep(3);                                                                                                                   /* Wait for 3s to allow settings to settle down */
		if (IOPMSleepSystem(root_power_port) == kIOReturnSuccess)                                                                   /* Request the system to sleep */
			CFRunLoopRun();                                                                                                            /* On success, start the run loop */
		else 
			perror("Failed to send the sleep request\n");                                                                              /* On failure, do not start it */
	}																															/* The run loop has stopped: system has woken up */
	
	if (restore) {                                                                                                              /* If we are asked to restore the original hibernate mode */
		if (SetHibernateMode(original_mode, current_ps)) {                                                                          /* Restore the original hibernate mode */
			printf("Failed to set the hibernation mode\n");
			SetActiveProfile(original_profile, current_ps, active_prof);
			PortsCleanup();
			CFCleanup();
			return 1;
		}
		if (SetActiveProfile(original_profile, current_ps, active_prof)) {                                                          /* Restore the original power profile */
			printf("Failed to set the active profile\n");
			PortsCleanup();
			CFCleanup();
			return 1;
		}
	}
																																   
	PortsCleanup();				                                                                                                /* Cleanup */																																																											
	CFCleanup();
    return 0;
}
