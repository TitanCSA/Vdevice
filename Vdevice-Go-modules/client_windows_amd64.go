//go:build windows && amd64

package vdevice

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"runtime"
	"sync"
	"syscall"
	"unsafe"
)

type NativeError struct {
	Code    uint32
	Message string
}

func (e *NativeError) Error() string { return fmt.Sprintf("%s (Windows %d)", e.Message, e.Code) }

type Client struct {
	mu                sync.Mutex
	dll               *syscall.DLL
	call, free, close *syscall.Proc
	id                uint64
}

func New() (*Client, error) { return NewWithLibrary("") }
func NewWithLibrary(path string) (*Client, error) {
	if path == "" {
		path = os.Getenv("VDEVICE_SDK_NATIVE_PATH")
	}
	if path == "" {
		var err error
		path, err = bundledLibrary()
		if err != nil {
			return nil, err
		}
	}
	dll, err := syscall.LoadDLL(path)
	if err != nil {
		return nil, err
	}
	fail := func(e error) (*Client, error) { dll.Release(); return nil, e }
	open, err := dll.FindProc("vdevice_json_open")
	if err != nil {
		return fail(err)
	}
	c := &Client{dll: dll}
	if c.call, err = dll.FindProc("vdevice_json_call"); err != nil {
		return fail(err)
	}
	if c.free, err = dll.FindProc("vdevice_json_free"); err != nil {
		return fail(err)
	}
	if c.close, err = dll.FindProc("vdevice_json_close"); err != nil {
		return fail(err)
	}
	code, _, _ := open.Call(uintptr(unsafe.Pointer(&c.id)))
	if code != 0 {
		return fail(&NativeError{uint32(code), "Cannot open SDK"})
	}
	return c, nil
}
func (c *Client) Call(operation string, arguments any) (json.RawMessage, error) {
	if arguments == nil {
		arguments = map[string]any{}
	}
	data, err := json.Marshal(map[string]any{"operation": operation, "arguments": arguments})
	if err != nil {
		return nil, err
	}
	if len(data) > 1024*1024 {
		return nil, errors.New("Request exceeds 1 MiB")
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.id == 0 {
		return nil, errors.New("SDK is closed")
	}
	pointer, _, _ := c.call.Call(uintptr(c.id), uintptr(unsafe.Pointer(&data[0])), uintptr(len(data)))
	runtime.KeepAlive(data)
	if pointer == 0 {
		return nil, errors.New("Native response allocation failed")
	}
	defer c.free.Call(pointer)
	// Borrowed external C memory is read only until the terminating NUL, then copied.
	var result []byte
	for offset := uintptr(0); offset <= 16*1024*1024; offset++ {
		b := *(*byte)(unsafe.Pointer(pointer + offset))
		if b == 0 {
			break
		}
		result = append(result, b)
	}
	var response struct {
		OK      bool            `json:"ok"`
		Result  json.RawMessage `json:"result"`
		Code    uint32          `json:"code"`
		Message string          `json:"message"`
	}
	decoder := json.NewDecoder(bytes.NewReader(result))
	if err = decoder.Decode(&response); err != nil {
		return nil, err
	}
	if !response.OK {
		return nil, &NativeError{response.Code, response.Message}
	}
	return response.Result, nil
}
func (c *Client) Close() error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.id == 0 {
		return nil
	}
	code, _, _ := c.close.Call(uintptr(c.id))
	if code != 0 {
		return &NativeError{uint32(code), "Native close failed"}
	}
	c.id = 0
	return c.dll.Release()
}
