use std::{ffi::{c_char,CStr,CString},fmt,ptr};
use serde_json::{json,Value};
#[cfg(not(all(windows,target_arch="x86_64")))]compile_error!("Windows x64 MSVC required");
#[link(name="VdeviceJsonSDK")]extern "C" {
 #[link_name="vdevice_json_open"]fn native_open(id:*mut u64)->u32;
 #[link_name="vdevice_json_close"]fn native_close(id:u64)->u32;
 #[link_name="vdevice_json_call"]fn native_call(id:u64,request:*const c_char,length:u32)->*mut c_char;
 #[link_name="vdevice_json_free"]fn native_free(response:*mut c_char);
}
#[derive(Debug)]pub struct Error{pub code:u32,pub message:String}
impl fmt::Display for Error{fn fmt(&self,f:&mut fmt::Formatter<'_>)->fmt::Result{write!(f,"{} (Windows {})",self.message,self.code)}}
impl std::error::Error for Error{}
pub struct Client{id:u64}
impl Client {
 pub fn new()->Result<Self,Error>{let mut id=0;let code=unsafe{native_open(&mut id)};if code!=0{return Err(Error{code,message:"Cannot open SDK".into()})}Ok(Self{id})}
 pub fn call(&mut self,operation:&str,arguments:Value)->Result<Value,Error>{
  if self.id==0{return Err(Error{code:6,message:"SDK is closed".into()})}
  let payload=json!({"operation":operation,"arguments":arguments}).to_string();if payload.len()>1024*1024{return Err(Error{code:87,message:"Request exceeds 1 MiB".into()})}
  let length=payload.len() as u32;let text=CString::new(payload).map_err(|e|Error{code:87,message:e.to_string()})?;let pointer=unsafe{native_call(self.id,text.as_ptr(),length)};if pointer.is_null(){return Err(Error{code:8,message:"Native response allocation failed".into()})}
  let raw=unsafe{CStr::from_ptr(pointer)}.to_bytes().to_vec();unsafe{native_free(pointer)};
  let response:Value=serde_json::from_slice(&raw).map_err(|e|Error{code:13,message:e.to_string()})?;
  if response["ok"]!=true{return Err(Error{code:response["code"].as_u64().unwrap_or(31) as u32,message:response["message"].as_str().unwrap_or("Native error").into()})}Ok(response["result"].clone())
 }
 pub fn close(&mut self)->Result<(),Error>{if self.id==0{return Ok(())}let code=unsafe{native_close(self.id)};if code!=0{return Err(Error{code,message:"Native close failed".into()})}self.id=0;Ok(())}
}
impl Drop for Client{fn drop(&mut self){let _=self.close();}}
