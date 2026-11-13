// SOURCE-ONLY adapter: original DATA body constructor/serializer statements
// byte-exact below; original super Request header is supplied by the peer seam.
package com.biglybt.core.dht.transport.udp.impl;
import java.io.*;
import com.biglybt.core.dht.transport.udp.DHTTransportUDP;
public final class OriginalDataBody {
  byte packet_type;byte[] transfer_key,key,data;int start_position,length,total_length;
  public byte getProtocolVersion(){return 55;}
  OriginalDataBody(){}
  OriginalDataBody(DataInputStream is)throws IOException {
		packet_type		= is.readByte();
		transfer_key	= DHTUDPUtils.deserialiseByteArray( is, 64 );

		int	max_key_size;

		if ( getProtocolVersion() >= DHTTransportUDP.PROTOCOL_VERSION_REPLICATION_CONTROL ){

			max_key_size = 255;

		}else{

			max_key_size = 64;
		}

		key				= DHTUDPUtils.deserialiseByteArray( is, max_key_size );
		start_position	= is.readInt();
		length			= is.readInt();
		total_length	= is.readInt();
		data			= DHTUDPUtils.deserialiseByteArray( is, 65535 );
  }
  public void serialise(DataOutputStream os)throws IOException {
		os.writeByte( packet_type );
		DHTUDPUtils.serialiseByteArray( os, transfer_key, 64 );

		int	max_key_size;

		if ( getProtocolVersion() >= DHTTransportUDP.PROTOCOL_VERSION_REPLICATION_CONTROL ){

			max_key_size = 255;

		}else{

			max_key_size = 64;
		}

		DHTUDPUtils.serialiseByteArray( os, key, max_key_size );
		os.writeInt( start_position );
		os.writeInt( length );
		os.writeInt( total_length );

		if ( data.length > 0 ){

			DHTUDPUtils.serialiseByteArray( os, data, start_position, length, 65535 );

		}else{

			DHTUDPUtils.serialiseByteArray( os, data,  65535 );
		}
  }
}
