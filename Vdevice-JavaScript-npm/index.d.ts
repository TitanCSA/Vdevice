export class NativeError extends Error { readonly code:number; }
export class Client { constructor(); call<T=unknown>(operation:string,arguments_?:Record<string,unknown>):T; close():void; }
