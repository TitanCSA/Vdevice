package vdevice

import (
	"encoding/json"
	"os"
	"testing"
)

func TestProduct(t *testing.T) {
	path := os.Getenv("SDK_TEST_FIXTURE")
	c, err := NewWithLibrary(path)
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	if _, err = c.Call("check", nil); err == nil {
		t.Fatal("invalid config accepted")
	}
	if path != "" {
		d, err := c.Call("connect", map[string]any{"descriptors": []any{map[string]any{"data": []int{1, 2, 3}}}})
		if err != nil {
			t.Fatal(err)
		}
		var device map[string]any
		if err = json.Unmarshal(d, &device); err != nil {
			t.Fatal(err)
		}
		if device["device_id"] != "9223372036854775809" {
			t.Fatal("uint64 lost")
		}
		if _, err = c.Call("send", map[string]any{"handle": device["handle"], "endpoint": 129, "data": []int{0, 1, 255}}); err != nil {
			t.Fatal(err)
		}
	}
}
