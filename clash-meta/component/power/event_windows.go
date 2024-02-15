package power

// modify from https://github.com/NeilSeligmann/G15Manager/blob/39a1b772ae8e215e316257ce57783839cb56f857/system/power/event.go

import (
	"unsafe"

	"golang.org/x/sys/windows"
)

// adapted from https://golang.org/src/runtime/os_windows.go

var (
	libPowrProf                              = windows.NewLazySystemDLL("powrprof.dll")
	powerRegisterSuspendResumeNotification   = libPowrProf.NewProc("PowerRegisterSuspendResumeNotification")
	powerUnregisterSuspendResumeNotification = libPowrProf.NewProc("PowerUnregisterSuspendResumeNotification")
)

func NewEventListener(cb func(Type)) (func(), error) {
	if err := powerRegisterSuspendResumeNotification.Find(); err != nil {
		return nil, err
	}
	if err := powerUnregisterSuspendResumeNotification.Find(); err != nil {
		return nil, nil
	}

	// Defines the type of event
	const (
		PBT_APMSUSPEND         uint32 = 4
		PBT_APMRESUMESUSPEND   uint32 = 7
		PBT_APMRESUMEAUTOMATIC uint32 = 18
	)

	const (
		_DEVICE_NOTIFY_CALLBACK = 2
	)
	type _DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS struct {
		callback uintptr
		context  uintptr
	}

	var fn interface{} = func(context uintptr, changeType uint32, setting uintptr) uintptr {
		switch changeType {
		case PBT_APMSUSPEND:
			cb(SUSPEND)
		case PBT_APMRESUMESUSPEND:
			cb(RESUME)
		case PBT_APMRESUMEAUTOMATIC:
			cb(RESUMEAUTOMATIC)
		}
		return 0
	}

	params := _DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS{
		callback: windows.NewCallback(fn),
	}
	handle := uintptr(0)

	_, _, err := powerRegisterSuspendResumeNotification.Call(
		_DEVICE_NOTIFY_CALLBACK,
		uintptr(unsafe.Pointer(&params)),
		uintptr(unsafe.Pointer(&handle)),
	)
	if err != nil {
		return nil, err
	}

	return func() {
		_, _, _ = powerUnregisterSuspendResumeNotification.Call(
			uintptr(unsafe.Pointer(&handle)),
		)
	}, nil
}
