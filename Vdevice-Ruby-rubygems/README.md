# Vdevice Ruby SDK

`gem install vdevice`

Windows x64 Ruby3.1+.

```ruby
require "vdevice"
VdeviceSDK::Client.open { |client| p client.call("status") }
```

[API and operation schema](docs/API.md). Matching driver installed separately.
