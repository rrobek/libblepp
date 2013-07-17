#include <iostream>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cassert>
#include <tuple>

#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>

#include <sys/socket.h>

#include <unistd.h>
#include "logging.h"
#include "lib/uuid.h"
extern "C"{
#include "att.h"
}
using namespace std;

#define LE_ATT_CID 4        //Spec 4.0 G.5.2.2
#define ATT_DEFAULT_MTU 23  //Spec 4.0 G.5.2.1

void test_fd_(int fd, int line)
{
	if(fd < 0)
	{
		cerr << "Error on line " << line << ": " <<strerror(errno) << endl;
		exit(1);
	}

	cerr << "Line " << line << " ok = " << fd  << endl;

}

string to_hex(const uint16_t& u)
{
	ostringstream os;
	os << setw(4) << setfill('0') << hex << u;
	return os.str();
}

string to_hex(const uint8_t& u)
{
	ostringstream os;
	os << setw(2) << setfill('0') << hex << (int)u;
	return os.str();
}

string to_str(const uint8_t& u)
{
	if(u < 32 || u > 126)
		return "\\x" + to_hex(u);
	else
	{
		char buf[] = {(char)u, 0};
		return buf;
	}
}

string to_str(const bt_uuid_t& uuid)
{
	//8 4 4 4 12
	if(uuid.type == BT_UUID16)
		return to_hex(uuid.value.u16);
	else if(uuid.type == BT_UUID128)
		return "--128--";
	else
		return "uuid.wtf";

}

string to_hex(const uint8_t* d, int l)
{
	ostringstream os;
	for(int i=0; i < l; i++)
		os << to_hex(d[i]) << " ";
	return os.str();
}
string to_hex(pair<const uint8_t*, int> d)
{
	return to_hex(d.first, d.second);
}

string to_hex(const vector<uint8_t>& v)
{
	return to_hex(v.data(), v.size());
}

string to_str(const uint8_t* d, int l)
{
	ostringstream os;
	for(int i=0; i < l; i++)
		os << to_str(d[i]);
	return os.str();
}
string to_str(pair<const uint8_t*, int> d)
{
	return to_str(d.first, d.second);
}
string to_str(pair<const uint8_t*, const uint8_t*> d)
{
	return to_str(d.first, d.second - d.first);
}

string to_str(const vector<uint8_t>& v)
{
	return to_str(v.data(), v.size());
}


#define test(X) test_fd_(X, __LINE__)
class ResponsePDU;

class ResponsePDU
{
	protected:
		void type_mismatch() const
		{	
			throw 1;
		}
	
	public:

		const uint8_t* data;
		int length;

		uint8_t uint8(int i) const
		{
			assert(i >= 0 && i < length);
			return data[i];
		}

		uint16_t uint16(int i) const
		{
			return uint8(i) | (uint8(i+1) << 8);
		}

		ResponsePDU(const uint8_t* d_, int l_)
		:data(d_),length(l_)
		{
		}

		uint8_t type() const 
		{
			return uint8(0);
		}
};

class PDUErrorResponse: public ResponsePDU
{
	public:
		PDUErrorResponse(const ResponsePDU& p_)
		:ResponsePDU(p_)
		{
			if(type()  != ATT_OP_ERROR)
				type_mismatch();
		}

		uint8_t request_opcode() const
		{
			return uint8(1);
		}

		uint16_t handle() const
		{
			return uint16(2);
		}

		uint8_t error_code() const
		{
			return uint8(4);
		}
		
		const char* error_str() const
		{
			return att_ecode2str(error_code());
		}
};


class PDUReadByTypeResponse: public ResponsePDU
{
	public:
		PDUReadByTypeResponse(const ResponsePDU& p_)
		:ResponsePDU(p_)
		{
			if(type()  != ATT_OP_READ_BY_TYPE_RESP)
				type_mismatch();

			if((length - 2) % element_size() != 0)
			{
				//Packet length is invalid.
				throw 1.;
			}
		}

		int value_size() const
		{
			return uint8(1) -2;
		}

		int element_size() const
		{
			return uint8(1);
		}

		int num_elements() const
		{
			return (length - 1) / element_size();
		}
		
		uint16_t handle(int i) const
		{
			return uint16(i*element_size() + 2);
		}

		pair<const uint8_t*, const uint8_t*> value(int i) const
		{
			const uint8_t* begin = data + i*element_size() + 4;
			return make_pair(begin, begin + value_size());
		}

		uint16_t value_uint16(int i) const
		{
			assert(value_size() == 2);
			return uint16(i*element_size()+4);
		}

};


class PDUReadGroupByTypeResponse: public ResponsePDU
{
	public:
		PDUReadGroupByTypeResponse(const ResponsePDU& p_)
		:ResponsePDU(p_)
		{
			if(type()  != ATT_OP_READ_BY_GROUP_RESP)
				type_mismatch();

			if((length - 2) % element_size() != 0)
			{
				//Packet length is invalid.
				LOG(Error, "Invalid packet length");
				throw 1.;
			}

			if(value_size() != 2 && value_size() != 16)
			{
				LOG(Error, "Invalid UUID length" << value_size());
				throw 1.;
			}
		}

		int value_size() const
		{
			return uint8(1) -4;
		}

		int element_size() const
		{
			return uint8(1);
		}

		int num_elements() const
		{
			return (length - 2) / element_size();
		}
		
		uint16_t start_handle(int i) const
		{
			return uint16(i*element_size() + 2);
		}

		uint16_t end_handle(int i) const
		{
			return uint16(i*element_size() + 4);
		}

		bt_uuid_t uuid(int i) const
		{
			const uint8_t* begin = data + i*element_size() + 6;

			bt_uuid_t uuid;
			if(value_size() == 2)
			{
				uuid.type = BT_UUID16;
				uuid.value.u16 = att_get_u16(begin);
			}
			else
			{
				uuid.type = BT_UUID128;
				uuid.value.u128 = att_get_u128(begin);
			}
				
			return uuid;
		}

		uint16_t value_uint16(int i) const
		{
			assert(value_size() == 2);
			return uint16(i*element_size()+4);
		}

};

void pretty_print(const ResponsePDU& pdu)
{
	if(log_level >= Debug)
	{
		cerr << "debug: ---PDU packet ---\n";
		cerr << "debug: " << to_hex(pdu.data, pdu.length) << endl;
		cerr << "debug: " << to_str(pdu.data, pdu.length) << endl;
		cerr << "debug: Packet type: " << to_hex(pdu.type()) << " " << att_op2str(pdu.type()) << endl;
		
		if(pdu.type() == ATT_OP_ERROR)
			cerr << "debug: " << PDUErrorResponse(pdu).error_str() << " in response to " <<  att_op2str(PDUErrorResponse(pdu).request_opcode()) << " on handle " + to_hex(PDUErrorResponse(pdu).handle()) << endl;
		else if(pdu.type() == ATT_OP_READ_BY_TYPE_RESP)
		{
			PDUReadByTypeResponse p(pdu);

			cerr << "debug: elements = " << p.num_elements() << endl;
			cerr << "debug: value size = " << p.value_size() << endl;

			for(int i=0; i < p.num_elements(); i++)
			{
				cerr << "debug: " << to_hex(p.handle(i)) << " ";
				if(p.value_size() != 2)
					cerr << "-->" << to_str(p.value(i)) << "<--" << endl;
				else
					cerr << to_hex(p.value_uint16(i)) << endl;
			}

		}
		else if(pdu.type() == ATT_OP_READ_BY_GROUP_RESP)
		{
			PDUReadGroupByTypeResponse p(pdu);
			cerr << "debug: elements = " << p.num_elements() << endl;
			cerr << "debug: value size = " << p.value_size() << endl;

			for(int i=0; i < p.num_elements(); i++)
				cerr << "debug: " <<  "[ " << to_hex(p.start_handle(i)) << ", " << to_hex(p.end_handle(i)) << ") :" << to_str(p.uuid(i)) << endl;
		}
		else
			cerr << "debug: --no pretty printer available--\n";
		
		cerr << "debug:\n";
	}
};

//Almost zero resource to represent the ATT protocol on a BLE
//device. This class does none of its own memory management, and will not generally allocate
//or do other nasty things. Oh no, it allocates a buffer!
//
//Mostly what it can do is write ATT command packets (PDUs) and receive PDUs back.
struct BLEDevice
{
	bool constructing;
	int sock;
	static const int buflen=ATT_DEFAULT_MTU;

	void test_fd_(int fd, int line)
	{
		if(fd < 0)
		{
			LOG(Info, "Error on line " << line << ": " <<strerror(errno));

			if(constructing && sock >= 0)
			{
				int ret = close(sock);
				if(ret < 0)
					LOG(Warning, "Error on line " << line << ": " <<strerror(errno));
				else
					LOG(Debug, "System call on " << line << ": " << strerror(errno));
			}
			exit(1);
		}
		else
			LOG(Debug, "System call on " << line << ": " << strerror(errno) << " ret = " << fd);
	}

	BLEDevice();

	void send_read_by_type(const bt_uuid_t& uuid, uint16_t start = 0x0001, uint16_t end=0xffff)
	{
		vector<uint8_t> buf(buflen);
		int len = enc_read_by_type_req(start, end, const_cast<bt_uuid_t*>(&uuid), buf.data(), buf.size());
		int ret = write(sock, buf.data(), len);
		test(ret);
	}

	void send_find_information(uint16_t start = 0x0001, uint16_t end=0xffff)
	{
		vector<uint8_t> buf(buflen);
		int len = enc_find_info_req(start, end, buf.data(), buf.size());
		int ret = write(sock, buf.data(), len);
		test(ret);
	}

	void send_read_group_by_type(const bt_uuid_t& uuid, uint16_t start = 0x0001, uint16_t end=0xffff)
	{
		vector<uint8_t> buf(buflen);
		int len = enc_read_by_grp_req(start, end, const_cast<bt_uuid_t*>(&uuid), buf.data(), buf.size());
		int ret = write(sock, buf.data(), len);
		test(ret);
	}

	ResponsePDU receive(uint8_t* buf, int max)
	{
		int len = read(sock, buf, max);
		test(len);
		pretty_print(ResponsePDU(buf, len));
		return ResponsePDU(buf, len);
	}

	ResponsePDU receive(vector<uint8_t>& v)
	{
		return receive(v.data(), v.size());
	}
};

//Easier to use implementation of the ATT protocol.
//Blocks, rather than chunking packets.
struct SimpleBlockingATTDevice: public BLEDevice
{
	template<class Ret, class PDUType, class E, class F, class G> 
	vector<Ret> read_multiple(const bt_uuid_t& uuid, int request, int response, const E& call,  const F& func, const G& last)
	{
		vector<Ret> ret;
		vector<uint8_t> buf(ATT_DEFAULT_MTU);
		
		int start=1;

		for(;;)
		{
			call(uuid, start, 0xffff);
			ResponsePDU r = receive(buf);

			if(r.type() == ATT_OP_ERROR)
			{
				PDUErrorResponse err = r;

				if(err.request_opcode() != request)
				{
					LOG(Error, string("Unexpected opcode in error. Expected ") + att_op2str(request) + " got "  + att_op2str(err.request_opcode()));
					throw 1;
				}
				else if(err.error_code() != ATT_ECODE_ATTR_NOT_FOUND)
				{
					LOG(Error, string("Received unexpected error:") + att_ecode2str(err.error_code()));
					throw 1;
				}
				else 
					break;
			}
			else if(r.type() != response)
			{
					LOG(Error, string("Unexpected response. Expected ") + att_op2str(response) + " got "  + att_op2str(r.type()));
			}
			else
			{
				PDUType t = r;
				for(int i=0; i < t.num_elements(); i++)
					ret.push_back(func(t, i));
				
				if(last(t) == 0xffff)
					break;
				else
					start = last(t)+1;

				LOG(Debug, "New start = " << start);
			}
		}

		return ret;

	}


	vector<pair<uint16_t, vector<uint8_t>>> read_by_type(const bt_uuid_t& uuid)
	{
		return read_multiple<pair<uint16_t, vector<uint8_t>>, PDUReadByTypeResponse>(uuid, ATT_OP_READ_BY_TYPE_REQ, ATT_OP_READ_BY_TYPE_RESP, 
			[&](const bt_uuid_t& u, int start, int end)
			{
				send_read_by_type(u, start, end);	
			},
			[](const PDUReadByTypeResponse& p, int i)
			{
				return make_pair(p.handle(i),  vector<uint8_t>(p.value(i).first, p.value(i).second));
			}, 
			[](const PDUReadByTypeResponse& p)
			{
				return p.handle(p.num_elements()-1);
			})
			;

	}

	vector<tuple<uint16_t, uint16_t, bt_uuid_t>> read_by_group_type(const bt_uuid_t& uuid)
	{
		return read_multiple<tuple<uint16_t, uint16_t, bt_uuid_t>, PDUReadGroupByTypeResponse>(uuid, ATT_OP_READ_BY_GROUP_REQ, ATT_OP_READ_BY_GROUP_RESP, 
			[&](const bt_uuid_t& u, int start, int end)
			{
				send_read_group_by_type(u, start, end);	
			},
			[](const PDUReadGroupByTypeResponse& p, int i)
			{
				return make_tuple(p.start_handle(i),  p.end_handle(i), p.uuid(i));
			},
			[](const PDUReadGroupByTypeResponse& p)
			{
				return p.end_handle(p.num_elements()-1);
			});
	}


};

template<class C> const C& haxx(const C& X)
{
	return X;
}

int haxx(uint8_t X)
{
	return X;
}
#define LOGVAR(X) LOG(Debug, #X << " = " << haxx(X))

BLEDevice::BLEDevice()
:constructing(true)
{
	//Allocate socket and create endpoint.
	sock = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
	test(sock);
	
	////////////////////////////////////////
	//Bind the socket
	//I believe that l2 is for an l2cap socket. These are kind of like
	//UDP in that they have port numbers and are packet oriented.
	struct sockaddr_l2 addr;
		
	bdaddr_t source_address = {{0,0,0,0,0,0}};  //i.e. the adapter. Note, 0, corresponds to BDADDR_ANY
	                                //However BDADDR_ANY uses a nonstandard C hack and does not compile
									//under C++. So, set it manually.
	//So, a sockaddr_l2 has the family (obviously)
	//a PSM (wtf?) 
	//  Protocol Service Multiplexer (WTF?)
	//an address (of course)
	//a CID (wtf) 
	//  Channel ID (i.e. port number?)
	//and an address type (wtf)

	memset(&addr, 0, sizeof(addr));
	addr.l2_family = AF_BLUETOOTH;

	addr.l2_psm = 0;
	addr.l2_cid = htobs(LE_ATT_CID);


	bacpy(&addr.l2_bdaddr, &source_address);

	//Address type: Low Energy public
	addr.l2_bdaddr_type=BDADDR_LE_PUBLIC;
	
	//Bind socket. This associates it with a particular adapter.
	//We chose ANY as the source address, so packets will go out of 
	//whichever adapter necessary.
	int ret = bind(sock, (sockaddr*)&addr, sizeof(addr));
	test(ret);
	

	////////////////////////////////////////
	
	//Need to do l2cap_set here
	l2cap_options options;
	unsigned int len = sizeof(options);
	memset(&options, 0, len);

	//Get the options with a minor bit of cargo culting.
	//SOL_L2CAP seems to mean that is should operate at the L2CAP level of the stack
	//L2CAP_OPTIONS who knows?
	ret = getsockopt(sock, SOL_L2CAP, L2CAP_OPTIONS, &options, &len);
	test(ret);

	LOGVAR(options.omtu);
	LOGVAR(options.imtu);
	LOGVAR(options.flush_to);
	LOGVAR(options.mode);
	LOGVAR(options.fcs);
	LOGVAR(options.max_tx);
	LOGVAR(options.txwin_size);


	
	//Can also use bacpy to copy addresses about
	str2ba("3C:2D:B7:85:50:2A", &addr.l2_bdaddr);
	ret = connect(sock, (sockaddr*)&addr, sizeof(addr));
	test(ret);


	//And this seems to work up to here.

	//Get the options with a minor bit of cargo culting.
	//SOL_L2CAP seems to mean that is should operate at the L2CAP level of the stack
	//L2CAP_OPTIONS who knows?
	ret = getsockopt(sock, SOL_L2CAP, L2CAP_OPTIONS, &options, &len);
	test(ret);

	LOGVAR(options.omtu);
	LOGVAR(options.imtu);
	LOGVAR(options.flush_to);
	LOGVAR(options.mode);
	LOGVAR(options.fcs);
	LOGVAR(options.max_tx);
	LOGVAR(options.txwin_size);


}

LogLevels log_level;

int main(int , char **)
{
	log_level = Trace;
	vector<uint8_t> buf(256);

	SimpleBlockingATTDevice b;
	
	bt_uuid_t uuid;
	uuid.type = BT_UUID16;

	uuid.value.u16 = 0x2800;
	
	
	log_level=Trace;

	auto r = b.read_by_type(uuid);

	for(unsigned int i=0; i < r.size(); i++)
	{
		cout << "Handle: " << to_hex(r[i].first) << ", Data: " << to_hex(r[i].second) << endl;
		cout <<                "-->" << to_str(r[i].second) << "<--" << endl;
	}



	auto s = b.read_by_group_type(uuid);
		
	for(unsigned int i=0; i < s.size(); i++)
	{
		cout << "Start: " << to_hex(get<0>(s[i]));
		cout << " End: " << to_hex(get<1>(s[i]));
		cout << " UUID: " << to_str(get<2>(s[i])) << endl;
	}


	/*b.send_read_by_type(uuid);
	b.receive(buf);

	uuid.value.u16 = 0x2901;
	b.send_read_by_type(uuid);
	b.receive(buf);

	uuid.value.u16 = 0x2803;
	b.send_read_by_type(uuid);
	b.receive(buf);

	b.send_read_by_type(uuid, 0x0008);
	b.receive(buf);
	b.send_read_by_type(uuid, 0x0010);
	b.receive(buf);
	b.send_read_by_type(uuid, 0x0021);
	b.receive(buf);


	cerr << endl<<endl<<endl;

	b.send_find_information();
	b.receive(buf);

	cerr << endl<<endl<<endl;
	cerr << endl<<endl<<endl;
	cerr << endl<<endl<<endl;
	uuid.value.u16 = 0x2800;
	b.send_read_by_type(uuid);
	b.receive(buf);
	b.send_read_group_by_type(uuid);
	b.receive(buf);
	b.send_read_group_by_type(uuid, 0x0013);
	b.receive(buf);
*/
}