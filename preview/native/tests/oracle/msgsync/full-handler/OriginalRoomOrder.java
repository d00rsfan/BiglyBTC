package com.vuze.plugins.azmsgsync;
import java.io.*;import java.util.*;import java.nio.ByteBuffer;import com.biglybt.core.util.ByteArrayHashMap;
// Exact original pure ordering blocks; host message records/diagnostic output are inert test adapters.
public final class OriginalRoomOrder {
 static final class Debug {static void out(String ignored){}}
 static final class ChatMessage {
  byte[] id,previous;long sequence,time;int uid;
  ChatMessage(int id,int prev,long sequence,long time,int uid){this.id=ByteBuffer.allocate(8).putLong(id).array();this.previous=prev<0?null:ByteBuffer.allocate(8).putLong(prev).array();this.sequence=sequence;this.time=time;this.uid=uid;}
  byte[] getID(){return id;}byte[] getPreviousID(){return previous;}void setPreviousID(byte[] value){previous=value;}long getSequence(){return sequence;}long getTimeStamp(){return time;}int getUID(){return uid;}
 }
 List<ChatMessage> messages=new ArrayList<>();
 List<ChatMessage> sort(){

			int	num_messages = messages.size();

			ByteArrayHashMap<ChatMessage>	id_map 		= new ByteArrayHashMap<>(num_messages);
			Map<ChatMessage,ChatMessage>	prev_map 	= new HashMap<>(num_messages);

			Map<ChatMessage,Object>	next_map 			= new HashMap<>(num_messages);

				// build id map so we can lookup prev messages

			// System.out.println( "Sorting messages" );

			for ( ChatMessage msg: messages ){

				// System.out.println( "    " + msg.getString());

				byte[]	id = msg.getID();

				id_map.put( id, msg );
			}

				// build sets of prev/next links

			for ( ChatMessage msg: messages ){

				byte[]	prev_id 	= msg.getPreviousID();

				if ( prev_id != null ){

					ChatMessage prev_msg = id_map.get( prev_id );

					if ( prev_msg != null ){

						msg.setPreviousID( prev_msg.getID());	// save some mem

						// ordering prev_msg::msg

						prev_map.put( msg, prev_msg );

						Object existing = next_map.get( prev_msg );

						if ( existing == null ){

							next_map.put( prev_msg, msg );

						}else if ( existing instanceof ChatMessage ){

							List<ChatMessage> list = new ArrayList<>();

							list.add( (ChatMessage)existing );
							list.add( msg );

							next_map.put( prev_msg,  list );

						}else{

							((List<ChatMessage>)existing).add( msg );
						}
					}
				}
			}

				// a comparator to consistently order messages to ensure sorting is determinstic

			Comparator<ChatMessage> message_comparator =
					new Comparator<ChatMessage>()
					{
						@Override
						public int
						compare(
							ChatMessage o1,
							ChatMessage o2 )
						{
							return( o1.getUID() - o2.getUID());
						}
					};

				// break any loops arbitrarily

			Set<ChatMessage>	linked_messages = new TreeSet<>(message_comparator);

			linked_messages.addAll( prev_map.keySet());

			while( linked_messages.size() > 0 ){

				ChatMessage start = linked_messages.iterator().next();

				linked_messages.remove( start );

				ChatMessage current = start;

				int	loops = 0;

				while( true ){

					loops++;

					if ( loops > num_messages ){

						Debug.out( "infinte loop" );

						break;
					}

					ChatMessage prev_msg = prev_map.get( current );

					if ( prev_msg == null ){

						break;

					}else{

						linked_messages.remove( prev_msg );

						if ( prev_msg == start ){

								// loopage

							prev_map.put( current, null );
							next_map.put( prev_msg, null );

							Debug.out( "Loopage" );

							break;

						}else{

							current = prev_msg;
						}
					}
				}

			}
				// find the heads of the various trees

			Set<ChatMessage>		tree_heads = new TreeSet<>(message_comparator);

			for ( ChatMessage msg: messages ){

				ChatMessage prev_msg = prev_map.get( msg );

				if ( prev_msg != null ){

					int	 loops = 0;

					while( true ){

						loops++;

						if ( loops > num_messages ){

							Debug.out( "infinte loop" );

							break;
						}

						ChatMessage prev_prev = prev_map.get( prev_msg );

						if ( prev_prev == null ){

							tree_heads.add( prev_msg );

							break;

						}else{

							prev_msg = prev_prev;
						}
					}
				}
			}

			// System.out.println( "Got trees: " + tree_heads.size());

			Set<ChatMessage>	remainder_set = new HashSet<>(messages);

			List<ChatMessage> result = null;

			for ( ChatMessage head: tree_heads ){

				List<ChatMessage>	chain = flattenTree( head, next_map, num_messages );

				remainder_set.removeAll( chain );

				if ( result == null ){

					result = chain;

				}else{

					result = merge( result, chain );
				}
			}

			if ( remainder_set.size() > 0 ){

					// these are messages not part of any chain so sort based on sequence or time

				List<ChatMessage>	with_seq	= new ArrayList<>(remainder_set.size());
				List<ChatMessage>	without_seq = new ArrayList<>(remainder_set.size());
			
				for (ChatMessage m: remainder_set ){
					
					if ( m.getSequence() == 0 && m.getPreviousID() == null ){
						
						without_seq.add( m );
						
					}else{
					
						with_seq.add( m );
					}
				}
				
				if ( !with_seq.isEmpty()){
				
					Collections.sort(
						with_seq,
						new Comparator<ChatMessage>()
						{
							@Override
							public int
							compare(
								ChatMessage m1,
								ChatMessage m2 )
							{
								long t1 = m1.getSequence();
								long t2 = m2.getSequence();

								if ( t1 == t2 ){
											
									t1 = m1.getTimeStamp();
									t2 = m2.getTimeStamp();
								}
							
								long l = t1 - t2;

								if ( l < 0 ){
									return( -1 );
								}else if ( l > 0 ){
									return( 1 );
								}else{
									return( m1.getUID() - m2.getUID());
								}
							}
						});
				}
				
				if ( !without_seq.isEmpty()){
					
					Collections.sort(
						without_seq,
						new Comparator<ChatMessage>()
						{
							@Override
							public int
							compare(
								ChatMessage m1,
								ChatMessage m2 )
							{
								long	t1 = m1.getTimeStamp();
								long	t2 = m2.getTimeStamp();
							
								long l = t1 - t2;

								if ( l < 0 ){
									return( -1 );
								}else if ( l > 0 ){
									return( 1 );
								}else{
									return( m1.getUID() - m2.getUID());
								}
							}
						});
				}
								
				if ( result == null ){

					result = with_seq;

				}else{

					result = merge( result, with_seq );
				}
				
					// finally merge in any timestamp only messages
									
				result = merge( result, without_seq );
			}

			if ( result == null ){

				return new ArrayList<ChatMessage>();
			}

return result;
}

		private List<ChatMessage>
		flattenTree(
			ChatMessage					head,
			Map<ChatMessage,Object>		next_map,
			int							num_messages )
		{
			if ( num_messages <= 0 ){

					// fail safe in case for some reason we end up in a loop

				return(new ArrayList<>());
			}

			List<ChatMessage> chain = new ArrayList<>(num_messages);

			ChatMessage msg = head;

			while( true ){

				chain.add( msg );

				num_messages--;

				Object entry = next_map.get( msg );

				if ( entry instanceof ChatMessage ){

					msg = (ChatMessage)entry;

				}else if ( entry instanceof List ){

					List<ChatMessage> list = (List<ChatMessage>)entry;

					List<ChatMessage> current = null;

					for ( ChatMessage node: list ){

						List<ChatMessage> temp = flattenTree( node, next_map, num_messages );

						num_messages -= temp.size();

						if ( current == null ){

							current = temp;

						}else{

							current = merge( current, temp );
						}
					}

					chain.addAll( current );

					break;

				}else{

					break;
				}
			}

			return( chain );
		}

		private List<ChatMessage>
		merge(
			List<ChatMessage>		list1,
			List<ChatMessage>		list2 )
		{
			if ( list1.isEmpty()){
				
				return( list2 );
				
			}else if ( list2.isEmpty()){
				
				return( list1 );
			}
			
			int	size1 = list1.size();
			int size2 = list2.size();

			int total = size1 + size2;
			
			List<ChatMessage>	result = new ArrayList<>(size1 + size2);

			if ( total == 0 ){
				
				return( result );
			}
			
			int	pos1 = 0;
			int pos2 = 0;

			while( true ){

				if ( pos1 == size1 ){

					for ( int i=pos2;i<size2;i++){

						result.add( list2.get(i));
					}

					break;

				}else if ( pos2 == size2 ){

					for ( int i=pos1;i<size1;i++){

						result.add( list1.get(i));
					}

					break;

				}else{

					ChatMessage m1 = list1.get( pos1 );
					ChatMessage m2 = list2.get( pos2 );

					long t1 = m1.getSequence();
					long t2 = m2.getSequence();

						// if sequence numbers are the same or either message is missing sequence + prev info then
						// we only have timestamp to use for ordering
					
					if (	( t1 == t2 ) ||
							( t1 == 0 && m1.getPreviousID() == null ) ||
							( t2 == 0 && m2.getPreviousID() == null )){
							
						t1 = m1.getTimeStamp();
						t2 = m2.getTimeStamp();
					}
					
					if ( t1 < t2 || ( t1 == t2 && m1.getUID() < m2.getUID())){

						result.add( m1 );

						pos1++;

					}else{

						result.add( m2 );

						pos2++;
					}
				}
			}

			return( result );
		}

 public static void main(String[] args)throws Exception{DataInputStream in=new DataInputStream(System.in);DataOutputStream out=new DataOutputStream(System.out);int cases=in.readInt();if(cases<0||cases>1024)throw new IOException("Bound");for(int c=0;c<cases;c++){OriginalRoomOrder o=new OriginalRoomOrder();int n=in.readInt();if(n<0||n>128)throw new IOException("Bound");for(int i=0;i<n;i++){o.messages.add(new ChatMessage(in.readInt(),in.readInt(),in.readLong(),in.readLong(),in.readInt()));}List<ChatMessage> result=o.sort();out.writeInt(result.size());for(ChatMessage m:result)out.writeInt(m.uid);}out.flush();}
}
