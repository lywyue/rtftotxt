#include <iostream>
#include <locale>
#include <string>
#include <cctype>
#include <fstream>
#include <cstring>
#include <map>
#include <string_view>
#include <sstream>
#include <vector>
#include <cassert>
#include <iconv.h>

using std::cout;
using std::endl;
using std::string;
using std::string_view;
using std::setlocale;
using std::mbstowcs;
using std::wcstombs;
using std::tolower;
using std::fstream;
using std::map;
using std::stringstream;
using std::isdigit;
using std::vector;
using std::to_string;

class ansicpg{
public:
	static map<uint, string_view> page;
	static map<uint, string_view> pagename;
	static uint getCodePage(string_view cpg);
	static string_view getCodePageName(uint code);
};

int gbk2utf8(string& utfstr, const char* srcstr);

class analysisrtf{
	enum class status{
		unknow,
		checkrtf,
		checkfontpage,
		checkcommand,
		obtainstring,
		obtainimage
	};

	enum class demande{
		ignore,
		obtain
	};

	union number{
	short		   flagn;
	unsigned short n;
	int zero;
	unsigned char c[4];
	};

	static const ushort _MAXLEN = 64;
public:
	analysisrtf():fontpage(""),filename(""){
		cd = iconv_open("utf-8", "utf-16");//获取转换句柄，void类型
	}

	analysisrtf(const char* path):fontpage(""),filename(""){
		open(path);
		cd = iconv_open("utf-8", "utf-16");//获取转换句柄，void类型
	}

	bool open(const char* path){
		handle.open(path, handle.in);
		if(!handle){
			cout << "open file " << path << " failure!" << endl;
			return false;
		}
		filename.append(path);
		return true;
	}

	void close(){
		handle.close();
		iconv_close(cd);
	}

	~analysisrtf(){
		close();
	}
	
	bool content(){
		while(!handle.eof()){
			switch(state){
			case status::unknow:
				if(!checkrtf()){
					return false;
				}
				// 状态由函数内部跳转
				break;
			case status::checkfontpage:
				checkfontpage();
				break;
			case status::checkcommand:
				checkcommand();
				break;
			case status::obtainstring:
				obtainstring();
				break;
			case status::obtainimage:
				obtainimg();
				break;
			}
		}
		return true;
	}

	void show(){
		for(auto it : result){
			cout << it;
		}
		cout << endl;
	}

private:
	bool checkrtf(){
		ushort sub = 0;
		char rtf[6] = {0};
		while(!handle.eof() && sub < 5){
			// {\rtf}
			rtf[sub] = handle.get();
			++sub;
		}
		if(0 == memcmp(rtf, "{\\rtf", 5)){
			rtf[sub] = handle.get();
			switch(rtf[sub]){
			case '1':
			case ' ':
			case '\\':
				state = status::checkfontpage;
				return true;
			}
		}
		return false;
	}

	void checkfontpage(){
		bool flag = false;
		ushort sit = 0;
		char* pos = &transitioncomm[0];
		while(!handle.eof()){
			transition = handle.get();

			switch(transition){
				case '\\':
				case ' ':
					if(!flag){
						if(0 == memcmp(transitioncomm, "ansicpg", 7)){
							for(auto i = 7; i < _MAXLEN && '\0' != transitioncomm[i]; ++i){
								sit = sit * 10 + transitioncomm[i] - '0';
							}
							flag = true;
							fontpage.append(setfontpage[sit]);
						} 
						memset(transitioncomm, 0, _MAXLEN);
						pos = &transitioncomm[0];
					}
					break;
				case '{':
					handle.unget();
					state = status::checkcommand;
					return ;
				default:
					if(!flag){
						*pos = transition;
						++pos;
					}					
					break;
			}
		}
	}

	void checkcommand(){
		// 由该函数区分firstcommand、obtaincommand、valuestring
		state = status::obtainstring;

		// 以'{'为第一个字符
		while(!handle.eof()){
			transition = handle.get();

			if(handle.eof()){
				return ;
			}

			switch(transition){
				case '\\':
					if(demande::obtain == issave){
						transition = handle.get();

						switch(transition){
							case 'u':
								transition = handle.get();

								if('-' == transition || isdigit(transition)){
									handle.unget();
									handle.unget();
									handle.unget();
									return ;
								}
								getcommand();
								break;
							case '\'':
							case '{':
							case '}':
								handle.unget();
								handle.unget();
								return ;
							case '*':
								break;
							default:
								handle.unget();
								getcommand();
								break;
						}
					}
					
					if(demande::ignore == issave){
						getcommand();
						if(0 == memcmp(transitioncomm, "blipuid", 7)){
							for(;'}' != handle.get();){}
							--count;
							state = status::obtainimage;
							return ;
						}
					}
					break;
				
				case '{':
					if(demande::obtain == issave){
						transition = handle.get();

						switch(transition){
						case '\\':
							transition = handle.get();
							switch(transition){
								case '*':
									handle.get(); // 丢弃一个'\\'
								break;
								default:
									handle.unget();
								break;
							}
							getcommand();
							if(setignore[transitioncomm]){
								issave = demande::ignore;
								count = 1;
							} else {
								issave = demande::obtain;
							}
							
							break;
						}
					} else {
						++count;
					}
					break;
				case '}':
					if(demande::ignore == issave){
						--count;
						if(0 == count){
							issave = demande::obtain;
						}
					}
					break;
				case '\n':
					break;
				default:
					if(demande::obtain == issave){
						handle.unget();
						return ;
					}
					break;
			}
		}
	}

	void getcommand(){
		char* pos = &transitioncomm[0];
		memset(transitioncomm, 0, _MAXLEN);

		while(!handle.eof()){
			transition = handle.get();

			if(handle.eof()){
				return ;
			}

			switch(transition){
			case '{':
			case '\\':
			case '}':
				handle.unget();
				return ;
			case ' ':
			case '\n':
				return ;
			default:
				*pos = transition;
				++pos;
				break;
			}
		}
	}

	void obtainstring(){
		string nowline("");
		char c;
		number un;
		bool flag = true;
		state = status::checkcommand;
		issave = demande::obtain;
		count = 0;

		while(!handle.eof()){
			transition = handle.get();

			if(handle.eof()){
				savestring(nowline);
				return ;
			}

			switch(transition){
			case '\\':
				transition = handle.get();

				switch(transition){
				case '\'':
					c = tonum(handle.get()) << 4;
					c |= tonum(handle.get());
					nowline.append(1, c);
					break;
				case 'u':
					transition = handle.get();

					un.zero = 0;
					if('-' == transition || isdigit(transition)){
						if('-' == transition){
							for(transition = handle.get(); isdigit(transition);){
								un.n = un.n * 10 + transition - '0';
							}
							un.flagn = (-un.flagn);
						} else {
							for(; isdigit(transition); transition = handle.get()){
								un.n = un.n * 10 + transition - '0';
							}
						}
						flag = true;

						string ss("");
						ss.append(1, un.c[1]);
						ss.append(1, un.c[0]);
						string temp = unicodetoutf8(ss);
						if(nowline.empty()){
							savedirectstring(temp);
						} else {
							savestring(nowline);
							string ("").swap(nowline);
							savedirectstring(temp);
						}
						
						// 跳出结尾词'?' 或 '\'3f'
						while(flag){
							switch(transition){
							case ' ':
								transition = handle.get();
								break;
							case '?':
								flag = false;
								break;
							case '\\':
								transition = handle.get();
								switch(transition){
									case '\'':
										if('3' == handle.get()){
											if('f' != handle.get()){
												handle.unget();
											}
										} else {
											handle.unget();
											handle.unget();
											handle.unget();
										}
									break;
									default:
										handle.unget();
										handle.unget();
										break;
								}
								flag = false;
								break;
							default:
								flag = false;
								handle.unget();
								break;
							}
						}
						
					} else {
						handle.unget();
						handle.unget();
						handle.unget();

						if(!nowline.empty()){
							savestring(nowline);
						}
						
						return ;
					}
					break;
				case '{':
				case '}':
				case '\\':
					nowline.append(1, transition);
					break;
				default:
					handle.unget();
					handle.unget();
					if(!nowline.empty()){
						savestring(nowline);
					}
					
					return;
				}
				break;
			case '}':
				if(!nowline.empty()){
					savestring(nowline);
				}
				return ;
			default:
				nowline.append(1, transition);
				break;
			}
		}
	}

	void obtainimg(){
		state = status::obtainstring;
		static uint seq = 1;

		string file("/tmp/.temporaryImage/");
		file.append(filename);
		file.append("__");
		file.append(to_string(seq));
		++seq;
		file.append(".jpeg");
		fstream writeimg(file, writeimg.out | writeimg.app);
		char c[2] = {0};
		bool sock = true;

		do{
			transition = handle.get();

			if('}' == transition){
				break;
			}

			if(sock){
				c[0] = tonum(transition) << 4;
				sock = false;
			} else {
				c[0] |= tonum(transition);
				sock = true;
				writeimg << c[0];
			}

		}while(true);
		writeimg.close();
	}

	void savestring(const string& line){
		if(!line.empty()){
			string broken("");
			converToutf8(broken, line);	
			result.emplace_back(broken);
		}
	}

	void savedirectstring(string& line){
		result.emplace_back(line);
	}

	string unicodetoutf8(const string& src){
		if (cd == 0)  
			return "";  

		size_t inlen = src.size();  
		size_t outlen = 15;  
		char* inbuf = (char*)src.c_str();  
		char outbuf[16];//这里实在不知道需要多少个字节，这是个问题  
		//char outbuf = new char[outlen]; 另外outbuf不能在堆上分配内存，否则转换失败，猜测跟iconv函数有关  
		memset(outbuf, 0, outlen);  
	
		char *poutbuf = outbuf; //多加这个转换是为了避免iconv这个函数出现char()[255]类型的实参与char**类型的形参不兼容  
		if (iconv(cd, &inbuf, &inlen, &poutbuf,&outlen) == -1)  
			return "";  
	
		return string(outbuf);
	}

	void converToutf8(string& dst, const string& src){
		if(0 == fontpage.compare("zh_CN.utf8")){
			dst.append(src);
			return ;
		}

		if(nullptr == setlocale(LC_ALL, fontpage.c_str())){
			cout << "Set locale to "<< fontpage << " failure!" << endl;
			return ;
		}

		int unicodelen = mbstowcs(nullptr, src.c_str(), 0);
		if(unicodelen <= 0){
			cout << fontpage << endl;
			cout << "Get length failure!" << endl;
			return ;
		}

		wchar_t* unicodestr = reinterpret_cast<wchar_t*>(new wchar_t[unicodelen + 1]);
		wmemset(unicodestr, L'\0', unicodelen + 1);
		mbstowcs(unicodestr, src.c_str(), src.length());

		if(nullptr == setlocale(LC_ALL, "zh_CN.utf8")){
			cout << "Set locale to utf8 failure!" << endl;
			return ;
		}

		int utflen = wcstombs(nullptr, unicodestr, 0);
		if(utflen <= 0){
			cout << "Get utf length failure!" << endl;
			return ;
		}
		char* p = new char[utflen + 1];
		memset(p, 0, utflen + 1);
		wcstombs(p, unicodestr, utflen);
		dst.append(p);
		delete[] p;
		delete[] unicodestr;
		unicodestr = nullptr;
		p = nullptr;

		return ;
	}

private:
	unsigned char tonum(char c)
	{
		if('/' < c && c < ':'){
			return static_cast<unsigned char>(c - '0');
		} else if(0x60 < c && c < 'g'){
			return static_cast<unsigned char>(c - 'a' + 10);
		}
	}
private:
	fstream handle;
	string filename;
	vector<string> result;
	string fontpage;
	char transitioncomm[_MAXLEN] = {0};
	uint leftcount = 0;
	status state = status::unknow;
	char transition;
	ushort count = 0;
	demande issave = demande::obtain;
	iconv_t cd;
	static map<ushort, string_view> setfontpage;
	static map<string, bool> setignore;
};



int main(int argc, char** argv)
{
	analysisrtf analy(argv[1]);

	if(!analy.content())
	{
		cout << "Not rtf file" << endl;
	} else {
		analy.show();
	}

	return 0;
}

map<ushort, string_view> analysisrtf::setfontpage = {
	{0, "zh_CN.utf8"},
	{936, "zh_CN.gbk"},
	{54936, "zh_CN.gb18030"}
};

map<string, bool> analysisrtf::setignore = {
	{"fonttbl", true},
	{"colortbl", true}, 
	{"stylesheet", true},
	{"latentstyles", true},
	{"list", true},
	{"listtable", true},
	{"generator", true},
	{"info", true},
	{"fchars", true},
	{"lchars", true},
	{"pgdsctbl", true},
	{"shppict", true}
};