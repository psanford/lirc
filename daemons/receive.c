/*      $Id: receive.c,v 5.4 2000/07/06 17:49:30 columbus Exp $      */

/****************************************************************************
 ** receive.c ***************************************************************
 ****************************************************************************
 *
 * functions that decode IR codes
 * 
 * Copyright (C) 1999 Christoph Bartelmus <lirc@bartelmus.de>
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "hardware.h"
#include "lircd.h"
#include "receive.h"

lirc_t readdata(void);

extern struct hardware hw;
extern struct ir_remote *last_remote;

struct rbuf rec_buffer;

inline lirc_t lirc_t_max(lirc_t a,lirc_t b)
{
	return(a>b ? a:b);
}

lirc_t get_next_rec_buffer(unsigned long maxusec)
{
	if(rec_buffer.rptr<rec_buffer.wptr)
	{
#               ifdef DEBUG
		logprintf(3,"<%lu\n",(unsigned long)
			  rec_buffer.data[rec_buffer.rptr]&(PULSE_MASK));
#               endif
		rec_buffer.sum+=rec_buffer.data[rec_buffer.rptr]&(PULSE_MASK);
		return(rec_buffer.data[rec_buffer.rptr++]);
	}
	else
	{
		if(rec_buffer.wptr<RBUF_SIZE)
		{
			lirc_t data;
			
			if(!waitfordata(maxusec)) return(0);
			data=readdata();
                        rec_buffer.data[rec_buffer.wptr]=data;
                        if(rec_buffer.data[rec_buffer.wptr]==0) return(0);
                        rec_buffer.sum+=rec_buffer.data[rec_buffer.rptr]
				&(PULSE_MASK);
                        rec_buffer.wptr++;
                        rec_buffer.rptr++;
#                       ifdef DEBUG
                        logprintf(3,"+%lu\n",(unsigned long)
				  rec_buffer.data[rec_buffer.rptr-1]
                                  &(PULSE_MASK));
#                       endif
                        return(rec_buffer.data[rec_buffer.rptr-1]);
		}
		else
		{
			rec_buffer.too_long=1;
			return(0);
		}
	}
	return(0);
}

int clear_rec_buffer()
{
	int move,i;

	if(hw.rec_mode==LIRC_MODE_LIRCCODE)
	{
		unsigned char buffer[sizeof(ir_code)];
		size_t count;
		
		count=hw.code_length/CHAR_BIT;
		if(hw.code_length%CHAR_BIT) count++;
		
		if(read(hw.fd,buffer,count)!=count)
		{
			logprintf(0,"reading in mode LIRC_MODE_LIRCCODE "
				  "failed\n");
			return(0);
		}
		for(i=0,rec_buffer.decoded=0;i<count;i++)
		{
			rec_buffer.decoded=(rec_buffer.decoded<<CHAR_BIT)+
			((ir_code) buffer[i]);
		}
	}
	else if(hw.rec_mode==LIRC_MODE_CODE)
	{
		unsigned char c;
		
		if(read(hw.fd,&c,1)!=1)
		{
			logprintf(0,"reading in mode LIRC_MODE_CODE "
				  "failed\n");
			return(0);
		}
		rec_buffer.decoded=(ir_code) c;
	}
	else
	{
		lirc_t data;
		
		move=rec_buffer.wptr-rec_buffer.rptr;
		if(move>0 && rec_buffer.rptr>0)
		{
			memmove(&rec_buffer.data[0],
				&rec_buffer.data[rec_buffer.rptr],
				sizeof(rec_buffer.data[0])*move);
			rec_buffer.wptr-=rec_buffer.rptr;
		}
		else
		{
			rec_buffer.wptr=0;
		}
		
		data=readdata();
		
#               ifdef DEBUG
		logprintf(3,"c%lu\n",(unsigned long) data&(PULSE_MASK));
#               endif
		
		rec_buffer.data[rec_buffer.wptr]=data;
		rec_buffer.wptr++;
	}
	rec_buffer.rptr=0;
	
	rec_buffer.too_long=0;
	rec_buffer.is_biphase=0;
	rec_buffer.pendingp=0;
	rec_buffer.pendings=0;
	rec_buffer.sum=0;
	
	return(1);
}

void rewind_rec_buffer()
{
	rec_buffer.rptr=0;
	rec_buffer.too_long=0;
	rec_buffer.pendingp=0;
	rec_buffer.pendings=0;
	rec_buffer.sum=0;
}

inline void unget_rec_buffer(int count)
{
	if(count==1 || count==2)
	{
		rec_buffer.rptr-=count;
		rec_buffer.sum-=rec_buffer.data[rec_buffer.rptr]&(PULSE_MASK);
		if(count==2)
		{
			rec_buffer.sum-=rec_buffer.data[rec_buffer.rptr+1]
			&(PULSE_MASK);
		}
	}
}

inline lirc_t get_next_pulse()
{
	lirc_t data;

	data=get_next_rec_buffer(0);
	if(data==0) return(0);
	if(!is_pulse(data))
	{
#               ifdef DEBUG
		logprintf(2,"pulse expected\n");
#               endif
		return(0);
	}
	return(data&(PULSE_MASK));
}

inline lirc_t get_next_space()
{
	lirc_t data;

	data=get_next_rec_buffer(0);
	if(data==0) return(0);
	if(!is_space(data))
	{
#               ifdef DEBUG
		logprintf(2,"space expected\n");
#               endif
		return(0);
	}
	return(data);
}

int expectpulse(struct ir_remote *remote,int exdelta)
{
	lirc_t deltas,deltap;
	int retval;
	
	if(rec_buffer.pendings>0)
	{
		deltas=get_next_space();
		if(deltas==0) return(0);
		retval=expect(remote,deltas,rec_buffer.pendings);
		if(!retval) return(0);
		rec_buffer.pendings=0;
	}
	
	deltap=get_next_pulse();
	if(deltap==0) return(0);
	if(rec_buffer.pendingp>0)
	{
		retval=expect(remote,deltap,
			      rec_buffer.pendingp+exdelta);
		if(!retval) return(0);
		rec_buffer.pendingp=0;
	}
	else
	{
		retval=expect(remote,deltap,exdelta);
	}
	return(retval);
}

int expectspace(struct ir_remote *remote,int exdelta)
{
	lirc_t deltas,deltap;
	int retval;

	if(rec_buffer.pendingp>0)
	{
		deltap=get_next_pulse();
		if(deltap==0) return(0);
		retval=expect(remote,deltap,rec_buffer.pendingp);
		if(!retval) return(0);
		rec_buffer.pendingp=0;
	}
	
	deltas=get_next_space();
	if(deltas==0) return(0);
	if(rec_buffer.pendings>0)
	{
		retval=expect(remote,deltas,
			      rec_buffer.pendings+exdelta);
		if(!retval) return(0);
		rec_buffer.pendings=0;
	}
	else
	{
		retval=expect(remote,deltas,exdelta);
	}
	return(retval);
}

inline int expectone(struct ir_remote *remote,int bit)
{
	if(is_biphase(remote))
	{
		if(is_rc6(remote) &&
		   remote->toggle_bit>0 &&
		   bit==remote->toggle_bit-1)
		{
			if(remote->sone>0 &&
			   !expectspace(remote,2*remote->sone))
			{
				unget_rec_buffer(1);
				return(0);
			}
			rec_buffer.pendingp=2*remote->pone;
		}
		else
		{
			if(remote->sone>0 && !expectspace(remote,remote->sone))
			{
				unget_rec_buffer(1);
				return(0);
			}
			rec_buffer.pendingp=remote->pone;
		}
	}
	else
	{
		if(remote->pone>0 && !expectpulse(remote,remote->pone))
		{
			unget_rec_buffer(1);
			return(0);
		}
		if(remote->ptrail>0)
		{
			if(remote->sone>0 &&
			   !expectspace(remote,remote->sone))
			{
				unget_rec_buffer(2);
				return(0);
			}
		}
		else
		{
			rec_buffer.pendings=remote->sone;
		}
	}
	return(1);
}

inline int expectzero(struct ir_remote *remote,int bit)
{
	if(is_biphase(remote))
	{
		if(is_rc6(remote) &&
		   remote->toggle_bit>0 &&
		   bit==remote->toggle_bit-1)
		{
			if(!expectpulse(remote,2*remote->pzero))
			{
				unget_rec_buffer(1);
				return(0);
			}
			rec_buffer.pendings=2*remote->szero;
			
		}
		else
		{
			if(!expectpulse(remote,remote->pzero))
			{
				unget_rec_buffer(1);
				return(0);
			}
			rec_buffer.pendings=remote->szero;
		}
	}
	else
	{
		if(!expectpulse(remote,remote->pzero))
		{
			unget_rec_buffer(1);
			return(0);
		}
		if(remote->ptrail>0)
		{
			if(!expectspace(remote,remote->szero))
			{
				unget_rec_buffer(2);
				return(0);
			}
		}
		else
		{
			rec_buffer.pendings=remote->szero;
		}
	}
	return(1);
}

inline lirc_t sync_rec_buffer(struct ir_remote *remote)
{
	int count;
	lirc_t deltas,deltap;
	
	count=0;
	deltas=get_next_space();
	if(deltas==0) return(0);
	
	if(last_remote!=NULL && !is_rcmm(remote))
	{
		while(deltas<last_remote->remaining_gap*
		      (100-last_remote->eps)/100 &&
		      deltas<last_remote->remaining_gap-last_remote->aeps)
		{
			deltap=get_next_pulse();
			if(deltap==0) return(0);
			deltas=get_next_space();
			if(deltas==0) return(0);
			count++;
			if(count>REC_SYNC) /* no sync found, 
					      let's try a diffrent remote */
			{
				return(0);
			}
		}
	}
	rec_buffer.sum=0;
	return(deltas);
}

inline int get_header(struct ir_remote *remote)
{
	if(is_rcmm(remote))
	{
		lirc_t deltap,deltas,sum;
		deltap=get_next_pulse();
		if(deltap==0)
		{
			unget_rec_buffer(1);
			return(0);
		}
		deltas=get_next_space();
		if(deltas==0)
		{
			unget_rec_buffer(2);
			return(0);
		}
		sum=deltap+deltas;
		if(expect(remote,sum,remote->phead+remote->shead))
		{
			return(1);
		}
		unget_rec_buffer(2);
		return(0);
	}
	if(!expectpulse(remote,remote->phead))
	{
		unget_rec_buffer(1);
		return(0);
	}
	if(!expectspace(remote,remote->shead))
	{
		unget_rec_buffer(2);
		return(0);
	}
	return(1);
}

inline int get_foot(struct ir_remote *remote)
{
	if(!expectspace(remote,remote->sfoot)) return(0);
	if(!expectpulse(remote,remote->pfoot)) return(0);
	return(1);
}

inline int get_lead(struct ir_remote *remote)
{
	if(remote->plead==0) return(1);
	rec_buffer.pendingp=remote->plead;
	return(1);	
}

inline int get_trail(struct ir_remote *remote)
{
	if(remote->ptrail!=0)
	{
		if(!expectpulse(remote,remote->ptrail)) return(0);
	}
	if(rec_buffer.pendingp>0)
	{
		if(!expectpulse(remote,0)) return(0);
	}
	return(1);
}

inline int get_gap(struct ir_remote *remote,lirc_t gap)
{
	lirc_t data;
	
#       ifdef DEBUG
	logprintf(2,"sum: %ld\n",rec_buffer.sum);
#       endif
	data=get_next_rec_buffer(gap*(100-remote->eps)/100);
	if(data==0) return(1);
	if(!is_space(data))
	{
#               ifdef DEBUG
		logprintf(2,"space expected\n");
#               endif
		return(0);
	}
	if(data<gap*(100-remote->eps)/100 &&
	   data<gap-remote->aeps)
	{
#               ifdef DEBUG
		logprintf(1,"end of signal not found\n");
#               endif
		return(0);
	}
	else
	{
		unget_rec_buffer(1);
	}
	return(1);	
}

inline int get_repeat(struct ir_remote *remote)
{
	if(!get_lead(remote)) return(0);
	if(is_biphase(remote))
	{
		if(!expectspace(remote,remote->srepeat)) return(0);
		if(!expectpulse(remote,remote->prepeat)) return(0);
	}
	else
	{
		if(!expectpulse(remote,remote->prepeat)) return(0);
		rec_buffer.pendings=remote->srepeat;
	}
	if(!get_trail(remote)) return(0);
	if(!get_gap(remote,
		    is_const(remote) ? 
		    (remote->gap>rec_buffer.sum ? remote->gap-rec_buffer.sum:0):
		    (has_repeat_gap(remote) ? remote->repeat_gap:remote->gap)
		    )) return(0);
	return(1);
}

ir_code get_data(struct ir_remote *remote,int bits,int done)
{
	ir_code code;
	int i;
	
	code=0;
	
	if(is_rcmm(remote))
	{
		lirc_t deltap,deltas,sum;
		
		if(bits%2 || done%2)
		{
			logprintf(0,"invalid bit number.\n");
			return((ir_code) -1);
		}
		for(i=0;i<bits;i+=2)
		{
			code<<=2;
			deltap=get_next_pulse();
			deltas=get_next_space();
			if(deltap==0 || deltas==0) 
			{
				logprintf(0,"failed on bit %d\n",done+i+1);
			}
			sum=deltap+deltas;
#                       ifdef DEBUG
			logprintf(3,"rcmm: sum %ld\n",(unsigned long) sum);
#                       endif
			if(expect(remote,sum,remote->pzero+remote->szero))
			{
				code|=0;
#                               ifdef DEBUG
				logprintf(2,"00\n");
#                               endif
			}
			else if(expect(remote,sum,remote->pone+remote->sone))
			{
				code|=1;
#                               ifdef DEBUG
				logprintf(2,"01\n");
#                               endif
			}
			else if(expect(remote,sum,remote->ptwo+remote->stwo))
			{
				code|=2;
#                               ifdef DEBUG
				logprintf(2,"10\n");
#                               endif
			}
			else if(expect(remote,sum,remote->pthree+remote->sthree))
			{
				code|=3;
#                               ifdef DEBUG
				logprintf(2,"11\n");
#                               endif
			}
			else
			{
#                               ifdef DEBUG
				logprintf(2,"no match for %ld+%ld=%ld\n",
					  deltap,deltas,sum);
#                               endif
				return((ir_code) -1);
			}
		}
		return(code);
	}

	for(i=0;i<bits;i++)
	{
		code=code<<1;
		if(expectone(remote,done+i))
		{
#                       ifdef DEBUG
			logprintf(2,"1\n");
#                       endif
			code|=1;
		}
		else if(expectzero(remote,done+i))
		{
#                       ifdef DEBUG
			logprintf(2,"0\n");
#                       endif
			code|=0;
		}
		else
		{
#                       ifdef DEBUG
			logprintf(1,"failed on bit %d\n",done+i+1);
#                       endif
			return((ir_code) -1);
		}
	}
	return(code);
}

ir_code get_pre(struct ir_remote *remote)
{
	ir_code pre;

	pre=get_data(remote,remote->pre_data_bits,0);

	if(pre==(ir_code) -1)
	{
#               ifdef DEBUG
		logprintf(1,"failed on pre_data\n");
#               endif
		return((ir_code) -1);
	}
	if(remote->pre_p>0 && remote->pre_s>0)
	{
		if(!expectpulse(remote,remote->pre_p))
			return((ir_code) -1);
		rec_buffer.pendings=remote->pre_s;
	}
	return(pre);
}

ir_code get_post(struct ir_remote *remote)
{
	ir_code post;

	if(remote->post_p>0 && remote->post_s>0)
	{
		if(!expectpulse(remote,remote->post_p))
			return((ir_code) -1);
		rec_buffer.pendings=remote->post_s;
	}

	post=get_data(remote,remote->post_data_bits,remote->pre_data_bits+
		      remote->bits);

	if(post==(ir_code) -1)
	{
#               ifdef DEBUG
		logprintf(1,"failed on post_data\n");
#               endif
		return((ir_code) -1);
	}
	return(post);
}

int receive_decode(struct ir_remote *remote,
		   ir_code *prep,ir_code *codep,ir_code *postp,
		   int *repeat_flagp,lirc_t *remaining_gapp)
{
	ir_code pre,code,post,code_mask=0,post_mask=0;
	lirc_t sync;
	int header;

	sync=0; /* make compiler happy */
	code=pre=post=0;
	header=0;

	if(hw.rec_mode==LIRC_MODE_MODE2 ||
	   hw.rec_mode==LIRC_MODE_PULSE ||
	   hw.rec_mode==LIRC_MODE_RAW)
	{
		rewind_rec_buffer();
		rec_buffer.is_biphase=is_biphase(remote) ? 1:0;
		
		/* we should get a long space first */
		if(!(sync=sync_rec_buffer(remote)))
		{
#                       ifdef DEBUG
			logprintf(1,"failed on sync\n");
#                       endif
			return(0);
		}

#               ifdef DEBUG
		logprintf(1,"sync\n");
#               endif

		if(has_repeat(remote) && last_remote==remote)
		{
			if(remote->flags&REPEAT_HEADER && has_header(remote))
			{
				if(!get_header(remote))
				{
#                                       ifdef DEBUG
					logprintf(1,"failed on repeat "
						  "header\n");
#                                       endif
					return(0);
				}
#                               ifdef DEBUG
				logprintf(1,"repeat header\n");
#                               endif
			}
			if(get_repeat(remote))
			{
				if(remote->last_code==NULL)
				{
					logprintf(0,"repeat code without last_code "
						  "received\n");
					return(0);
				}

				*prep=remote->pre_data;
				*codep=remote->last_code->code;
				*postp=remote->post_data;
				*repeat_flagp=1;

				*remaining_gapp=
				is_const(remote) ? 
				(remote->gap>rec_buffer.sum ?
				 remote->gap-rec_buffer.sum:0):
				(has_repeat_gap(remote) ?
				 remote->repeat_gap:remote->gap);
				return(1);
			}
			else
			{
#                               ifdef DEBUG
				logprintf(1,"no repeat\n");
#                               endif
				rewind_rec_buffer();
				sync_rec_buffer(remote);
			}

		}

		if(has_header(remote))
		{
			header=1;
			if(!get_header(remote))
			{
				header=0;
				if(!(remote->flags&NO_HEAD_REP && 
				     (sync<=remote->gap+remote->gap*remote->eps/100
				      || sync<=remote->gap+remote->aeps)))
				{
#                                       ifdef DEBUG
					logprintf(1,"failed on header\n");
#                                       endif
					return(0);
				}
			}
#                       ifdef DEBUG
			logprintf(1,"header\n");
#                       endif
		}
	}

	if(is_raw(remote))
	{
		struct ir_ncode *codes,*found;
		int i;

		if(hw.rec_mode==LIRC_MODE_CODE ||
		   hw.rec_mode==LIRC_MODE_LIRCCODE)
			return(0);

		codes=remote->codes;
		found=NULL;
		while(codes->name!=NULL && found==NULL)
		{
			found=codes;
			for(i=0;i<codes->length;)
			{
				if(!expectpulse(remote,codes->signals[i++]))
				{
					found=NULL;
					rewind_rec_buffer();
					sync_rec_buffer(remote);
					break;
				}
				if(i<codes->length &&
				   !expectspace(remote,codes->signals[i++]))
				{
					found=NULL;
					rewind_rec_buffer();
					sync_rec_buffer(remote);
					break;
				}
			}
			codes++;
		}
		if(found!=NULL)
		{
			if(!get_gap(remote,
				    is_const(remote) ? 
				    remote->gap-rec_buffer.sum:
				    remote->gap)) 
				found=NULL;
		}
		if(found==NULL) return(0);
		code=found->code;
	}
	else
	{
		if(hw.rec_mode==LIRC_MODE_CODE ||
		   hw.rec_mode==LIRC_MODE_LIRCCODE)
		{
			int i;
 			lirc_t sum;
 			struct timeval current;

#                       ifdef DEBUG
#                       ifdef LONG_IR_CODE
			logprintf(1,"decoded: %llx\n",rec_buffer.decoded);
#                       else
			logprintf(1,"decoded: %lx\n",rec_buffer.decoded);
#                       endif		
#                       endif
			if((hw.rec_mode==LIRC_MODE_CODE &&
			    hw.code_length<remote->pre_data_bits
			    +remote->bits+remote->post_data_bits)
			   ||
			   (hw.rec_mode==LIRC_MODE_LIRCCODE && 
			    hw.code_length!=remote->pre_data_bits
			    +remote->bits+remote->post_data_bits))
			{
				return(0);
			}
			
			for(i=0;i<remote->post_data_bits;i++)
			{
				post_mask=(post_mask<<1)+1;
			}
			post=rec_buffer.decoded&post_mask;
			post_mask=0;
			rec_buffer.decoded=
			rec_buffer.decoded>>remote->post_data_bits;
			for(i=0;i<remote->bits;i++)
			{
				code_mask=(code_mask<<1)+1;
			}
			code=rec_buffer.decoded&code_mask;
			code_mask=0;
			pre=rec_buffer.decoded>>remote->bits;
			gettimeofday(&current,NULL);
			sum=remote->phead+remote->shead+
				lirc_t_max(remote->pone+remote->sone,
					   remote->pzero+remote->szero)*
				(remote->bits+
				 remote->pre_data_bits+
				 remote->post_data_bits)+
				remote->plead+
				remote->ptrail+
				remote->pfoot+remote->sfoot+
				remote->pre_p+remote->pre_s+
				remote->post_p+remote->post_s;
			
			rec_buffer.sum=sum>=remote->gap ? remote->gap-1:sum;
			sync=time_elapsed(&remote->last_send,&current)-
 				rec_buffer.sum;
		}
		else
		{
			if(!get_lead(remote))
			{
#                               ifdef DEBUG
				logprintf(1,"failed on leading pulse\n");
#                               endif
				return(0);
			}
			
			if(has_pre(remote))
			{
				pre=get_pre(remote);
				if(pre==(ir_code) -1)
				{
#                                       ifdef DEBUG
					logprintf(1,"failed on pre\n");
#                                       endif
					return(0);
				}
#                               ifdef DEBUG
#                               ifdef LONG_IR_CODE
				logprintf(1,"pre: %llx\n",pre);
#                               else
				logprintf(1,"pre: %lx\n",pre);
#                               endif
#                               endif
			}
			
			code=get_data(remote,remote->bits,
				      remote->pre_data_bits);
			if(code==(ir_code) -1)
			{
#                               ifdef DEBUG
				logprintf(1,"failed on code\n");
#                               endif
				return(0);
			}
#                       ifdef DEBUG
#                       ifdef LONG_IR_CODE
			logprintf(1,"code: %llx\n",code);
#                       else
			logprintf(1,"code: %lx\n",code);
#                       endif
#                       endif
			
			if(has_post(remote))
			{
				post=get_post(remote);
				if(post==(ir_code) -1)
				{
#                                       ifdef DEBUG
					logprintf(1,"failed on post\n");
#                                       endif
					return(0);
				}
#                               ifdef DEBUG
#                               ifdef LONG_IR_CODE
				logprintf(1,"post: %llx\n",post);
#                               else
				logprintf(1,"post: %lx\n",post);
#                               endif
#                               endif
			}
			if(!get_trail(remote))
			{
#                               ifdef DEBUG
				logprintf(1,"failed on trailing pulse\n");
#                               endif
				return(0);
			}
			if(has_foot(remote))
			{
				if(!get_foot(remote))
				{
#                                       ifdef DEBUG
					logprintf(1,"failed on foot\n");
#                                       endif
					return(0);
				}
			}
			if(header==1 && is_const(remote) &&
			   (remote->flags&NO_HEAD_REP))
			{
				rec_buffer.sum-=remote->phead+remote->shead;
			}
			if(is_rcmm(remote))
			{
				if(!get_gap(remote,1000))
					return(0);
			}
			else if(is_const(remote))
			{
				if(!get_gap(remote,
					    remote->gap>rec_buffer.sum ?
					    remote->gap-rec_buffer.sum:0))
					return(0);
			}
			else
			{
				if(!get_gap(remote,remote->gap))
					return(0);
			}
		} /* end of mode specific code */
	}
	*prep=pre;*codep=code;*postp=post;
	if(sync<=remote->remaining_gap*(100+remote->eps)/100
	   || sync<=remote->remaining_gap+remote->aeps)
		*repeat_flagp=1;
	else
		*repeat_flagp=0;
	if(is_const(remote))
	{
		*remaining_gapp=remote->gap>rec_buffer.sum ?
			remote->gap-rec_buffer.sum:0;
	}
	else
	{
		*remaining_gapp=remote->gap;
	}
	return(1);
}