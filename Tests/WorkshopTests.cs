using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Web.Script.Serialization;

namespace Vdevice.Example
{
    // Same managed serializer and native bridge as the GUI. Only VdUiValidate
    // is called; no application startup, driver open, create, send or remove.
    internal static class WorkshopTests
    {
        private static int assertions;
        private static void Check(bool value, string message) { ++assertions; if (!value) throw new InvalidOperationException(message); }
        private static void Reject(Action action, string message)
        {
            bool rejected = false;
            try { action(); } catch (FormatException) { rejected = true; }
            Check(rejected, message);
        }
        private static uint Validate(byte[] bytes, uint length)
        {
            StringBuilder message = new StringBuilder(8192); return Native.VdUiValidate(bytes, length, message, 8192);
        }
        private static DescriptorRow Descriptor(string kind, string index, string language, string iface, string bytes)
        { return new DescriptorRow { Kind = kind, Index = index, Language = language, Interface = iface, Bytes = bytes }; }
        private static WorkshopDocument Fixture()
        {
            return new WorkshopDocument
            {
                Name = "Raw descriptor roundtrip Δ", Speed = 2, InputDepth = 7, DelegateControls = true,
                Descriptors = new[] {
                    Descriptor("Standard","0","0000","0","12 01 00 02 00 00 00 40 FF FF 10 00 00 01 01 00 00 01"),
                    Descriptor("Standard","0","0000","0","09 02 29 00 01 01 00 80 32 09 04 00 00 02 03 00 00 00 09 21 11 01 00 01 22 1D 00 07 05 81 03 10 00 01 07 05 01 03 10 00 01"),
                    Descriptor("Report","0","0000","0","06 00 FF 09 01 A1 01 15 00 26 FF 00 75 08 95 10 09 02 81 02 09 03 91 02 09 04 B1 02 C0"),
                    Descriptor("Standard","0","0000","0","06 03 09 04 0C 04"),
                    Descriptor("Standard","1","0409","0","04 03 41 00"),
                    Descriptor("Standard","1","040C","0","04 03 42 00")
                },
                Rules = new[] {
                    new RuleRow { Setup="C0 5A 00 00 00 00 00 00",Mask="FF FF FF FF FF FF 00 00",Action="Reply",Bytes="56 44 01 00" },
                    new RuleRow { Setup="40 5B 00 00 00 00 00 00",Mask="FF FF FF FF FF FF FF FF",Action="Accept",Bytes="" },
                    new RuleRow { Setup="40 5C 00 00 00 00 00 00",Mask="FF FF FF FF FF FF FF FF",Action="Stall",Bytes="" }
                }
            };
        }
        private static void ManagedTests()
        {
            Check(Workshop.Number("0x040C",65535,true)==0x040C,"Hex language selector");
            Check(Workshop.Number("7",256,false)==7,"Decimal input depth");
            Reject(delegate { Workshop.Number("-1",255,false); },"Negative selector refused");
            Reject(delegate { Workshop.Number("10000",65535,true); },"Oversized selector refused");
            Check(Workshop.Hex("00-01, 0x7f\r\n80\tFF",5).SequenceEqual(new byte[]{0,1,127,128,255}),"Mixed separators preserve bytes");
            Check(Workshop.Hex("",4096).Length==0,"Empty transfer data preserved");
            foreach(string invalid in new[]{"A","100","GG","0x","0x0","0000"}) Reject(delegate { Workshop.Hex(invalid,4096); },"Malformed hex refused: "+invalid);
            Reject(delegate { Workshop.Hex("00 01",1); },"Transfer size limit refused");
            int changed = 0; DescriptorRow row = new DescriptorRow(); row.PropertyChanged += delegate { ++changed; }; row.Bytes="AA";
            Check(changed==1,"Descriptor preview notified after editor change");
            changed=0; RuleRow rule=new RuleRow();rule.PropertyChanged+=delegate{++changed;};rule.Bytes="BB";
            Check(changed==1,"Rule preview notified after editor change");
            Check(Marshal.SizeOf(typeof(Native.Packet))==4136,"C# packet ABI is 4136 bytes");
            Check(Marshal.OffsetOf(typeof(Native.Packet),"Setup").ToInt32()==32,"Setup ABI offset");
            Check(Marshal.OffsetOf(typeof(Native.Packet),"Data").ToInt32()==40,"Data ABI offset");
            byte[] all=new byte[4096];for(int i=0;i<all.Length;++i)all[i]=(byte)i;
            OutputItem item=new OutputItem { Received=DateTime.UtcNow, Packet=new Native.Packet { DeviceId=9,RequestId=3,Kind=2,Endpoint=0,Length=4096,Setup=new byte[8],Data=all } };
            Check(item.Bytes.EndsWith("FD FE FF",StringComparison.Ordinal),"Full 4096-byte OUT payload retained");
            Check(item.Bytes.Split(' ').Length==4096,"OUT display does not truncate bytes");
        }
        private static void DocumentTests(string directory, string examplePath)
        {
            JavaScriptSerializer json=new JavaScriptSerializer(); WorkshopDocument fixture=Fixture(); byte[] blob=Workshop.Serialize(fixture);
            string text=json.Serialize(fixture); WorkshopDocument restored=json.Deserialize<WorkshopDocument>(text);
            Check(restored.Name==fixture.Name,"Unicode local label roundtrip");
            Check(Workshop.Serialize(restored).SequenceEqual(blob),"Every descriptor/rule selector and byte survives JSON roundtrip");
            WorkshopDocument example=json.Deserialize<WorkshopDocument>(File.ReadAllText(examplePath,Encoding.UTF8));
            Check(Workshop.Serialize(example).SequenceEqual(blob),"Shipped editable JSON matches the independently defined raw test fixture");
            File.WriteAllText(Path.Combine(directory,"roundtrip.json"),text,new UTF8Encoding(false));
            File.WriteAllBytes(Path.Combine(directory,"workshop.bin"),blob);
            using(BinaryReader input=new BinaryReader(new MemoryStream(blob))) {
                Check(input.ReadUInt32()==0x31574456u,"Private workshop format magic");
                Check(input.ReadUInt32()==2 && input.ReadUInt32()==7 && input.ReadUInt32()==1,"Speed, queue depth and delegation preserved");
                Check(input.ReadUInt32()==6 && input.ReadUInt32()==3,"Arbitrary descriptor/rule counts preserved");
                for(int i=0;i<6;++i) {
                    uint kind=input.ReadUInt32(),index=input.ReadUInt32(),language=input.ReadUInt32(),iface=input.ReadUInt32(),length=input.ReadUInt32();
                    Check(kind==(i==2?2u:1u) && index==(i>=4?1u:0u) && language==(i==4?0x0409u:i==5?0x040Cu:0u) && iface==0,"Raw descriptor selectors preserved at "+i);
                    byte[] data=input.ReadBytes((int)length);
                    Check(data.SequenceEqual(Workshop.Hex(fixture.Descriptors[i].Bytes,65536)),"Raw descriptor bytes preserved at "+i);
                }
                for(int i=0;i<3;++i) {
                    Check(input.ReadBytes(8).SequenceEqual(Workshop.Hex(fixture.Rules[i].Setup,8)),"Exact rule setup preserved");
                    Check(input.ReadBytes(8).SequenceEqual(Workshop.Hex(fixture.Rules[i].Mask,8)),"Exact rule mask preserved");
                    Check(input.ReadUInt32()==(uint)(i+1),"Rule action preserved");uint length=input.ReadUInt32();
                    Check(input.ReadBytes((int)length).SequenceEqual(Workshop.Hex(fixture.Rules[i].Bytes,4096)),"Exact rule reply preserved");
                }
                Check(input.BaseStream.Position==blob.Length,"No hidden fields or trailing bytes");
            }
            Check(Validate(blob,(uint)blob.Length)==0,"Actual bridge and raw SDK accept valid exact workshop blob");
            foreach(int limit in new[]{0,1,2,23}) Check(Validate(blob,(uint)limit)!=0,"Too-short framing refused");
            for(int length=24;length<blob.Length;++length)Check(Validate(blob,(uint)length)!=0,"Every truncated workshop record refused");
            byte[] oversized=new byte[65537];Check(Validate(oversized,(uint)oversized.Length)!=0,"Oversized bridge config refused");
            byte[] trailing=blob.Concat(new byte[]{0}).ToArray();Check(Validate(trailing,(uint)trailing.Length)!=0,"Unparsed trailing byte refused");
            foreach(int offset in new[]{0,4,8,12,16,20}) {
                byte[] invalid=(byte[])blob.Clone();Array.Copy(BitConverter.GetBytes(offset==16?65u:offset==20?33u:offset==12?2u:0u),0,invalid,offset,4);
                Check(Validate(invalid,(uint)invalid.Length)!=0,"Invalid header scalar/count refused at "+offset);
            }
            WorkshopDocument empty=new WorkshopDocument();byte[] emptyBlob=Workshop.Serialize(empty);Check(Validate(emptyBlob,(uint)emptyBlob.Length)!=0,"Empty descriptor document is not a valid device");
            WorkshopDocument wrong=Fixture();wrong.Descriptors[2].Interface="1";byte[] wrongBlob=Workshop.Serialize(wrong);Check(Validate(wrongBlob,(uint)wrongBlob.Length)!=0,"Incorrect report interface selector refused");
            wrong=Fixture();wrong.Descriptors[5].Language="0409";wrongBlob=Workshop.Serialize(wrong);Check(Validate(wrongBlob,(uint)wrongBlob.Length)!=0,"Duplicate descriptor selector refused");
            wrong=Fixture();wrong.Descriptors[1].Bytes=wrong.Descriptors[1].Bytes.Replace("09 02 29 00","09 02 28 00");wrongBlob=Workshop.Serialize(wrong);Check(Validate(wrongBlob,(uint)wrongBlob.Length)!=0,"Mismatched wTotalLength refused without repair");
            wrong=Fixture();wrong.Rules[1].Bytes="01";wrongBlob=Workshop.Serialize(wrong);Check(Validate(wrongBlob,(uint)wrongBlob.Length)!=0,"Nonempty Accept reply refused without rewriting");
            wrong=Fixture();wrong.Schema=2;Reject(delegate{Workshop.Serialize(wrong);},"Unknown JSON schema refused");
            wrong=Fixture();wrong.Descriptors=null;Reject(delegate{Workshop.Serialize(wrong);},"Missing descriptor list refused");
            wrong=Fixture();wrong.Rules[0].Setup="00";Reject(delegate{Workshop.Serialize(wrong);},"Short control setup refused");
            wrong=Fixture();wrong.Descriptors[0].Index="256";Reject(delegate{Workshop.Serialize(wrong);},"Descriptor index overflow refused");
            uint state=0x1F30A94Du;
            for(int iteration=0;iteration<25000;++iteration) {
                byte[] changed=(byte[])blob.Clone();
                for(int edit=0;edit<3;++edit) { state^=state<<13;state^=state>>17;state^=state<<5;int offset=4+(int)(state%(uint)(changed.Length-4));changed[offset]^=(byte)(1u<<(int)(state%8)); }
                Validate(changed,(uint)changed.Length);++assertions;
            }
        }
        private static int Main(string[] args)
        {
            try {
                if(args.Length!=2)throw new ArgumentException("Output directory and example JSON required.");
                Directory.CreateDirectory(args[0]); ManagedTests();DocumentTests(args[0],args[1]);
                string result="{\"passed\":true,\"openedDriver\":false,\"createdDevices\":false,\"assertions\":"+assertions.ToString(CultureInfo.InvariantCulture)+"}";
                File.WriteAllText(Path.Combine(args[0],"results.json"),result,new UTF8Encoding(false));Console.WriteLine(result);return 0;
            } catch(Exception error) {Console.Error.WriteLine(error);return 1;}
        }
    }
}
