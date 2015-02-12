#include <node.h>
#include "v8.h"

#include <string.h>
#include "entities.hpp"
#include "acc_translit.hpp"


#include <stdlib.h>
#include <ctype.h>

#include <vector>
#include "htmlstrip.hpp"

using namespace v8;

enum states {
	IN_TEXT,
	IN_TAG,
	IN_COMMENT,
	IN_SCRIPT,
	IN_STYLE,
	IN_ATTRIBUTE
};

enum tagtype {
	TAG_ANY,
	TAG_SCRIPT_START,
	TAG_STYLE_START,
	TAG_COMMENT,
	TAG_ATTRIBUTE,
	TAG_HINT_END
};

extern Persistent<String> chars_written_sym;
extern Persistent<String> include_script_sym;
extern Persistent<String> include_style_sym;
extern Persistent<String> compact_whitespace_sym;
extern Persistent<String> include_attributes_sym;

extern Persistent<String> taghints_sym;
extern Persistent<String> taghints_pos_sym;
extern Persistent<String> taghints_type_sym;
extern Persistent<String> taghints_type_script_sym;
extern Persistent<String> taghints_type_style_sym;
extern Persistent<String> taghints_type_any_sym;
extern Persistent<String> taghints_type_attribute_sym;
extern Persistent<String> taghints_type_end_sym;


#define IS_SCRIPT_OPEN(inBuf,i,numInChars) \
	( \
		(i+7 < numInChars) \
		&& tolower(inBuf[i+1]) == 's' && tolower(inBuf[i+2]) == 'c' && tolower(inBuf[i+3]) == 'r' \
		&& tolower(inBuf[i+4]) == 'i' && tolower(inBuf[i+5]) == 'p' && tolower(inBuf[i+6]) == 't' \
		&& !isalpha(inBuf[i+7]) \
	)

#define IS_SCRIPT_CLOSE(inBuf,i,numInChars) \
	(\
		(i+8 < numInChars) \
		&& inBuf[i+1] == '/' \
		&& tolower(inBuf[i+2]) == 's' && tolower(inBuf[i+3]) == 'c' && tolower(inBuf[i+4]) == 'r' \
		&& tolower(inBuf[i+5]) == 'i' && tolower(inBuf[i+6]) == 'p' && tolower(inBuf[i+7]) == 't' \
		&& !isalpha(inBuf[i+8]) \
	)

#define IS_STYLE_OPEN(inBuf,i,numInChars) \
	( \
		(i+6 < numInChars) \
		&& tolower(inBuf[i+1]) == 's' && tolower(inBuf[i+2]) == 't' && tolower(inBuf[i+3]) == 'y' \
		&& tolower(inBuf[i+4]) == 'l' && tolower(inBuf[i+5]) == 'e' \
		&& !isalpha(inBuf[i+6]) \
	)

#define IS_STYLE_CLOSE(inBuf,i,numInChars) \
	(\
		(i+7 < numInChars) \
		&& inBuf[i+1] == '/' \
		&& tolower(inBuf[i+2]) == 's' && tolower(inBuf[i+3]) == 't' && tolower(inBuf[i+4]) == 'y' \
		&& tolower(inBuf[i+5]) == 'l' && tolower(inBuf[i+6]) == 'e' \
		&& !isalpha(inBuf[i+7]) \
	)

#define APPEND_WS_COMPACT(b) if(*(b-1) != ' ') { *b++ = ' '; };

#if defined(_MSC_VER)
#define strtoll _strtoi64
#endif


#if (NODE_MAJOR_VERSION == 0) && (NODE_MINOR_VERSION < 12)

#else

#endif


// convert a html entity
// inBuf - input buffer
// i - position in input buffer, should point at the '&'
// outBuf - output buffer where to place the entity character
// return true on successfull conversion, false on no match
static inline bool
decode_html_entity(uint16_t* inBuf, size_t &i,const size_t numInChars, uint16_t* &outBuf){
	switch(inBuf[i+1]){
		case '\x09': break;
		case '\x0a': break;
		case '\x0c': break;
		case '\x20': break;
		case '\x3c': break;
		case '\x26': break;
		case '#':{
			size_t j = i+2;
			bool base16 = false;
			if(tolower(inBuf[j]) == 'x'){
				base16 = true;
				++j;
			}
			size_t ns = j;
			char str[32];
			int k = 0;
			str[0] = 0;
			for(k=0; ns < numInChars && (k < 30); ++k){
				uint16_t c = tolower(inBuf[ns]);
				if(c >= '0' && c <= '9'){
					str[k] = (char)inBuf[ns++];
					continue;
				}
				if(base16 && c >= 'a' && c <= 'f'){
					str[k] = (char)inBuf[ns++];
					continue;
				}
				break;
			}
			if(k==0){
				// parse error
				return false;
			}
			str[k] = 0;
			uint16_t code = static_cast<uint16_t>(strtoll(str,NULL,base16?16:10));
			*outBuf++ = code;
			i = ns;
			return true;
		}break;
		default:{
			// Longest HTML entity is 32 chars
			char str[34];
			size_t ns = i+1;
			int k;
			str[0] = 0;
			for(k=0; ns < numInChars && isalnum(inBuf[ns]) && (k < 32); ++k){
				str[k] = static_cast<char>(inBuf[ns++]);
			}
			if(k>0){
				// lookup entity here
				if(inBuf[ns] == ';'){
					str[k] = static_cast<char>(inBuf[ns++]);
					str[++k] = 0;
				} else {
					str[k] = 0;
				}
				const struct entity *e = EntityLookup::lookup_entity(str,k);
				if(e){
					*outBuf++ = e->code[0];
					if(e->code[1]){
						*outBuf++ = e->code[1];
					}
					i = ns-1;
					return true;
				}
				// now try for all combinations, that don't end with ;
				for(int l=k-1; l>=2 && !e; --l){
					--ns;
					str[l] = 0;
					e = EntityLookup::lookup_entity(str,l);
				}
				if(e){
					*outBuf++ = e->code[0];
					if(e->code[1]){
						*outBuf++ = e->code[1];
					}
					i = ns-1;
					return true;
				}
			}
			// parse error
			return false;
		}break;
	}
	return false;
}

struct TagPoint {
	int pos;
	enum tagtype tag;
	TagPoint(int p, enum tagtype t) {
		pos = p;
		tag = t;
	}
};


Handle<Value> HtmlStrip(uint16_t* inBuf, size_t inBufSize, HtmlStripOptions opts) {

		if(!inBuf){
			return ThrowException(
            Exception::TypeError(
							String::New("HtmlStrip: Arguments must be a UTF-16 encoded Buffer, and length"))
        );
		}

		// Create output buffer
	  Local<Object> global = v8::Context::GetCurrent()->Global();
	  Local<Value> bv = global->Get(String::NewSymbol("Buffer"));
	  assert(bv->IsFunction());
	  Local<Function> b = Local<Function>::Cast(bv);

	  Local<Value> argv[1] = { Number::New(double(inBufSize)) };
	  Local<Object> outBuffer = b->NewInstance(1, argv);

		// create extra info array
		std::vector<TagPoint> tagPoints;

		const uint16_t* outBufBegin = static_cast<uint16_t*>(
			outBuffer->GetIndexedPropertiesExternalArrayData());
		uint16_t* outBuf = const_cast<uint16_t*>(outBufBegin);

		if(opts.compact_whitespace){
		// Insert one space at the begginng so the lookback doesn't fail
			*outBuf++ = ' ';
		}
		// Baby take off your tags , real fast :)
		size_t numInChars = inBufSize/2;
		int state = IN_TEXT;
		int tagType = TAG_ANY;
		uint16_t openAttribute = 0;

		#define START_TAG(offset,t) 	tagPoints.push_back( TagPoint(offset + (outBuf - outBufBegin - 1),t) );
		START_TAG(1,TAG_ANY);

		for(size_t i=0; i<numInChars; ++i){
			switch(inBuf[i]){
				case '<':
					if(state == IN_SCRIPT || state == IN_STYLE){
						if( state == IN_SCRIPT && IS_SCRIPT_CLOSE(inBuf,i,numInChars) ){
							state = IN_TEXT;
							i+=7;
							START_TAG(1,TAG_ANY);
						} else if (state == IN_STYLE && IS_STYLE_CLOSE(inBuf,i,numInChars) ){
							state = IN_TEXT;
							i+=6;
							START_TAG(1,TAG_ANY);
						} else {
							break;
						}
						// if we found a close tag, consume the rest of it
						if(state == IN_TEXT){
							while(inBuf[i+1] != '>'){
								++i;
							}
						}
					}else if( (i+3 < numInChars) && inBuf[i+1] == '!' && inBuf[i+2] == '-' && inBuf[i+3] == '-' ){
						state = IN_COMMENT;
						tagType = TAG_COMMENT;
					}else if( IS_SCRIPT_OPEN(inBuf,i,numInChars) ){
							state = IN_TAG;
							tagType = TAG_SCRIPT_START;
					}else if( IS_STYLE_OPEN(inBuf,i,numInChars) ){
							state = IN_TAG;
							tagType = TAG_STYLE_START;
					}else if(state != IN_COMMENT ){
						state = IN_TAG;
						tagType = TAG_ANY;
					} else {
						continue;
					}
					if(opts.compact_whitespace){
						APPEND_WS_COMPACT(outBuf);
					}else {
						*outBuf++ = ' ';
					}
					continue;
				case '>':
					if(state == IN_SCRIPT || state == IN_STYLE){
						break;
					}else  if(state == IN_COMMENT){
						if(inBuf[i-1] == '-' && inBuf[i-2] == '-' ){
							state = IN_TEXT;
						}
					}else	if(state == IN_TAG && tagType == TAG_SCRIPT_START){
						state = IN_SCRIPT;
						START_TAG(0,TAG_SCRIPT_START);
					}else	if(state == IN_TAG && tagType == TAG_STYLE_START){
						state = IN_STYLE;
						START_TAG(0,TAG_STYLE_START);
					} else {
						state = IN_TEXT;
					}
					continue;
				case '&':
					if( (state == IN_TEXT || state == IN_SCRIPT || state== IN_ATTRIBUTE) && (i+2)<numInChars ){
						// Handle unicode character code
						if(decode_html_entity(inBuf,i,numInChars,outBuf)){
							continue;
						}
					}break;
				case '=':
					if( state == IN_TAG && opts.include_attributes ){
						bool includeThisAttr = opts.include_all_attributes;

						if( !opts.include_all_attributes ) {
							// construct the attribute name
							uint16_t attrName[256];
							int k , j = i;
							for(--j; isspace(inBuf[j]) && j >= 0 ; --j);
							// This is not very strict check for attribute name
							for(;
									inBuf[j] > ' ' &&
									inBuf[j] != '"' && inBuf[j] != '\'' &&
									inBuf[j] != '>' && inBuf[j] != '/'  &&
									inBuf[j] != '=' &&
									j >= 0
									; --j);
							++j;
							for(k=0; !isspace(inBuf[j]) && inBuf[j] != '=' && (k < 255); ++k, ++j){
								attrName[k] = inBuf[j];
							}
							attrName[k] = 0;

							// set found flag
							Local< String > attributeName = String::New(attrName, k);
							includeThisAttr = opts.includeAttributesMap->Has(attributeName);
						}
						if ( includeThisAttr ){
							state = IN_ATTRIBUTE;
							START_TAG(0,TAG_ATTRIBUTE);
							for(++i; isspace(inBuf[i]); ++i);
							if( inBuf[i] == '"' || inBuf[i] == '\'') {
								openAttribute = inBuf[i];
								++i;
							}
						}
					}break;
				// handle whitespace as defined in \s :
				//		https://developer.mozilla.org/en-US/docs/Web/JavaScript/Guide/Regular_Expressions
				case ' ':
				case '\f':
				case '\n':
				case '\r':
				case '\t':
				case '\v':
				case 0x00A0:
				case 0x1680:
				case 0x180e:
				case 0x2000:
				case 0x2001:
				case 0x2002:
				case 0x2003:
				case 0x2004:
				case 0x2005:
				case 0x2006:
				case 0x2007:
				case 0x2008:
				case 0x2009:
				case 0x200a:
				case 0x2028:
				case 0x2029:
				case 0x202f:
				case 0x205f:
				case 0x3000:
					if(opts.compact_whitespace){
						APPEND_WS_COMPACT(outBuf);
						continue;
					}
					break;
				default: break;
			}
			switch(state){
				case IN_STYLE:
					if(opts.include_style){
						*outBuf++ = inBuf[i];
					}
					break;
				case IN_SCRIPT:
					if(opts.include_script){
						*outBuf++ = inBuf[i];
					}
					break;
				case IN_ATTRIBUTE:
					if( (openAttribute == inBuf[i] || (openAttribute == 0 && isspace(inBuf[i]) )) ){
						state = IN_TAG;
						START_TAG(0,TAG_ANY);
						openAttribute = 0;
						if(opts.compact_whitespace){
							APPEND_WS_COMPACT(outBuf);
						}else {
							*outBuf++ = ' ';
						}
					} else {
						*outBuf++ = inBuf[i];
					}
					break;
				case IN_TEXT:
					*outBuf++ = inBuf[i];
					break;
				default: break;
			}
		}
		// Append a tag for closing the previsous one
		START_TAG(1,TAG_HINT_END);
		#undef START_TAG
		// Set the number of characters written
		outBuffer->Set(chars_written_sym, Integer::New(outBuf - outBufBegin) );
		// set the extra tag info
		Handle<Array> tagInfo = Array::New(tagPoints.size());
		for(size_t i=0; i < tagPoints.size(); ++i){
			Handle<Object> pos = Object::New();
			pos->Set(taghints_pos_sym, Integer::New(tagPoints[i].pos));
			switch(tagPoints[i].tag){
				case TAG_SCRIPT_START:
					pos->Set(taghints_type_sym, taghints_type_script_sym);
					break;
				case TAG_STYLE_START:
					pos->Set(taghints_type_sym, taghints_type_style_sym);
					break;
				case TAG_HINT_END:
					pos->Set(taghints_type_sym, taghints_type_end_sym);
					break;
				case TAG_ATTRIBUTE:
					pos->Set(taghints_type_sym, taghints_type_attribute_sym);
					break;
				default:
					pos->Set(taghints_type_sym, taghints_type_any_sym);
					break;
			}
			tagInfo->Set(i,pos);
		}
		outBuffer->Set(taghints_sym, tagInfo);

		// Return the buffer with stripped text
    return outBuffer;
}

Handle<Value> HtmlEntitiesDecode(uint16_t* inBuf, size_t inBufSize) {

		if(!inBuf){
			return ThrowException(
            Exception::TypeError(
							String::New("ConvertHtmlEntities: Arguments must be a UTF-16 encoded Buffer, and length"))
        );
		}

		// Create output buffer
	  Local<Object> global = v8::Context::GetCurrent()->Global();
	  Local<Value> bv = global->Get(String::NewSymbol("Buffer"));
	  assert(bv->IsFunction());
	  Local<Function> b = Local<Function>::Cast(bv);

	  Local<Value> argv[1] = { Number::New(double(inBufSize)) };
	  Local<Object> outBuffer = b->NewInstance(1, argv);


		uint16_t* outBuf = static_cast<uint16_t*>(
			outBuffer->GetIndexedPropertiesExternalArrayData());

		size_t numInChars = inBufSize/2;
		for(size_t i=0; i<numInChars; ++i){
			switch(inBuf[i]){
				case '&':
					if(decode_html_entity(inBuf,i,numInChars,outBuf)){
						continue;
					}
					break;
				default: break;
			}
			*outBuf++ = inBuf[i];
		}
		// Set the number of characters written
		outBuffer->Set(chars_written_sym,
			Integer::New(outBuf - static_cast<uint16_t*>(
				outBuffer->GetIndexedPropertiesExternalArrayData()))
		);
    return outBuffer;
}

// Covert accenteds char to its ascii representation,
// this may produce longer output string, than the output
Handle<Value> AccentedCharsNormalize(uint16_t* inBuf, size_t inBufSize) {

		if(!inBuf){
			return ThrowException(
            Exception::TypeError(
							String::New("AccentedCharsNormalize: Arguments must be a UTF-16 encoded Buffer, and length"))
        );
		}

		// Create output buffer
	  Local<Object> global = v8::Context::GetCurrent()->Global();
	  Local<Value> bv = global->Get(String::NewSymbol("Buffer"));
	  assert(bv->IsFunction());
	  Local<Function> b = Local<Function>::Cast(bv);

		// Allocate double size buffer , as in the worst case output string will be double legnth
	  Local<Value> argv[1] = { Number::New(double(inBufSize * 2)) };
	  Local<Object> outBuffer = b->NewInstance(1, argv);


		uint16_t* outBuf = static_cast<uint16_t*>(
			outBuffer->GetIndexedPropertiesExternalArrayData());

		size_t numInChars = inBufSize/2;
		for(size_t i=0; i<numInChars; ++i){
			if(transliterate_accented_norm(inBuf, i, outBuf)){
				continue;
			}
			*outBuf++ = inBuf[i];
		}
		// Set the number of characters written
		outBuffer->Set(chars_written_sym,
			Integer::New(outBuf - static_cast<uint16_t*>(
				outBuffer->GetIndexedPropertiesExternalArrayData()))
		);
		return outBuffer;
}

// remove accents from accented chars
// this may produce longer output string, than the output
Handle<Value> AccentedCharsStrip(uint16_t* inBuf, size_t inBufSize) {

		if(!inBuf){
			return ThrowException(
            Exception::TypeError(
							String::New("AccentedCharsNormalize: Arguments must be a UTF-16 encoded Buffer, and length"))
        );
		}

		// Create output buffer
	  Local<Object> global = v8::Context::GetCurrent()->Global();
	  Local<Value> bv = global->Get(String::NewSymbol("Buffer"));
	  assert(bv->IsFunction());
	  Local<Function> b = Local<Function>::Cast(bv);

		// Allocate double size buffer , as in the worst case output string will be double legnth
	  Local<Value> argv[1] = { Number::New(double(inBufSize * 2)) };
	  Local<Object> outBuffer = b->NewInstance(1, argv);


		uint16_t* outBuf = static_cast<uint16_t*>(
			outBuffer->GetIndexedPropertiesExternalArrayData());

		size_t numInChars = inBufSize/2;
		for(size_t i=0; i<numInChars; ++i){
			if(transliterate_accented_strip(inBuf, i, outBuf)){
				continue;
			}
			*outBuf++ = inBuf[i];
		}
		// Set the number of characters written
		outBuffer->Set(chars_written_sym,
			Integer::New(outBuf - static_cast<uint16_t*>(
				outBuffer->GetIndexedPropertiesExternalArrayData()))
		);
    return outBuffer;
}
