
#include <vector>
#include <string>
#include <map>
#include <iostream>
#include <fstream>
#include <cstring>
#include <memory.h>
#include <sys/mman.h>
#include <cstdint>


typedef void (*JittedFunc)(char*, char*, char*);

std::string Simplify(const std::string & code);
JittedFunc JIT(const std::string & code);
void Run(JittedFunc f);
	
int main(int argc, char ** argv)
{
	std::string code;
	if (argc == 2)
	{
		std::ifstream in(argv[1]);
		if ( ! in.good())
			return -1;
		for (std::string str ; std::getline(in, str) ; )
			code.append(str);
		in.close();
	}
	else
	{
		for (int c=std::cin.get() ; c!=EOF && c!='!' ; c=std::cin.get())
			code.push_back(c);
	}

	code = Simplify(code);
	JittedFunc f = JIT(code);
	if (f == 0)
		return -1;
	Run(f);

	return 0;
}

// custom instructions
#define JZ    0
#define JNZ   1
#define JZL   2
#define JNZL  3
#define ZERO  4
#define WIND  5
#define REWD  6
#define REWD2 7
#define WIND2 8

#define MULMAP 9
#define SET    10
#define ADDMAP 11

#define IN ','
#define OUT '.'

void Remove(std::string & code, const char * str)
{
	for (size_t pos = code.find(str) ; pos != std::string::npos ; pos = code.find(str))
		code.erase(pos, strlen(str));
}
void Replace(std::string & code, const std::string & series, char op)
{
	for (size_t pos = code.find(series) ; pos != std::string::npos ; pos = code.find(series))
		code.replace(pos, series.size(), 1, op);
}

std::string Simplify(const std::string & code)
{
	std::string result;
	for (auto it=code.begin(),end=code.end() ; it!=end ; ++it)
	{
		switch(*it)
		{
			case '+':
			case '-':
			case '>':
			case '<':
			case '.':
			case ',':
			case '[':
			case ']':
				result.push_back(*it);
			default:
				break;
		}
	}

	Remove(result, "+-");
	Remove(result, "-+");
	Remove(result, "<>");
	Remove(result, "><");
	Replace(result, "][-]", ']');
	Replace(result, "][+]", ']');
	Replace(result, "[-]", ZERO);
	Replace(result, "[+]", ZERO);
	Replace(result, std::string("-").append(1, (char)ZERO), ZERO);
	Replace(result, std::string("+").append(1, (char)ZERO), ZERO);
	Replace(result, std::string(1, (char)ZERO).append(","), ',');
	Replace(result, "+,", ',');
	Replace(result, "-,", ',');
	Replace(result, "[<]", REWD);
	Replace(result, "[>]", WIND);
	Replace(result, "[<<]", REWD2);
	Replace(result, "[>>]", WIND2);

	return result;
}


// Allocates RW memory of given size and returns a pointer to it. On failure,
// prints out the error and returns NULL. Unlike malloc, the memory is allocated
// on a page boundary so it's suitable for calling mprotect.
void* alloc_writable_memory(size_t size)
{
	void* ptr = mmap(0, size,
		           PROT_READ | PROT_WRITE,
		           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (ptr == (void*)-1)
		return 0;
	return ptr;
}
// Sets a RX permission on the given memory, which must be page-aligned. Returns
// 0 on success. On failure, prints out the error and returns -1.
int make_memory_executable(void* m, size_t size)
{
	if (mprotect(m, size, PROT_READ | PROT_EXEC) == -1)
		return -1;
	return 0;
}

JittedFunc JIT(const std::string & code)
{
	// mem_ptr = rdi
	// IN = rsi
	// OUT = rdx
	// pc = rcx
	// end = r8

	std::vector<std::vector<int>> ops;
	for (auto it=&*code.begin(),end=&*code.end() ; it!=end ; ++it)
	{
		if (*it == '+' || *it == '-' || *it == '<' || *it == '>')
		{
			auto it_beginning = it;
			int shift = 0;
			std::map<int,int> add_map;
			for ( ; it!=end && (*it == '+' || *it == '-' || *it == '<' || *it == '>') ; ++it)
			{
				switch(*it)
				{
					case '+':
						++add_map[shift];
						break;
					case '-':
						--add_map[shift];
						break;
					case '>':
						++shift;
						break;
					case '<':
						--shift;
						break;
					default:
						break;
				}
			}
			if (   shift == 0
			    && add_map[0] == -1
			    && it_beginning != &*code.begin()
			    && it_beginning[-1] == '['
			    && it != end
			    && *it == ']'   )
			{
				// actually a mul map
				std::vector<int> & op = ops.back(); // Replace '['
				op.clear();
				op.push_back(MULMAP);
				add_map.erase(add_map.find(0));
				for(auto am_it=add_map.begin(),am_end=add_map.end() ; am_it!=am_end ; ++am_it)
				{
					if (am_it->second != 0)
					{
						op.push_back(am_it->first);
						op.push_back(am_it->second);
					}
				}
				continue; // skip ']'
			}
			else
			{
				// simple add map
				int local_shift = 0;
				if (   ops.size() >= 2
				    && ops[ops.size()-2][0] == ADDMAP
				    && ops[ops.size()-1][0] == SET   )
				{
					// example: +<+<[-]<-<-
					local_shift = ops[ops.size()-2][1];
					ops[ops.size()-2][1] = 0;
					ops[ops.size()-1][2] += local_shift;
				}
				ops.push_back(std::vector<int>());
				std::vector<int> & op = ops.back();
				op.push_back(ADDMAP);
				op.push_back(shift + local_shift);
				for(auto am_it=add_map.begin(),am_end=add_map.end() ; am_it!=am_end ; ++am_it)
				{
					if (am_it->second != 0)
					{
						op.push_back(am_it->first + local_shift);
						op.push_back(am_it->second);
					}
				}
			}
		}

		if (it == end)
			break;

		ops.push_back(std::vector<int>());
		std::vector<int> & op = ops.back();
		switch(*it)
		{
			case ZERO:
				op.push_back(SET);
				op.push_back(0); // to 0
				for ( ; it[1] == '+' ; ++it)
					op.back() = (op.back()+1) % 256;
				op.push_back(0); // at offset 0
			case ']':
				op.push_back(int(*it));
				if (it[1] == '+')
				{
					ops.push_back(std::vector<int>());
					std::vector<int> & op = ops.back();
					op.push_back(SET);
					op.push_back(1);
					for (++it ; it[1] == '+' ; ++it)
						op.back() = (op.back()+1) % 256;
					op.push_back(0); // at offset 0
				}
				break;
			default:
				op.push_back(int(*it));
				break;
		}
	}

	size_t jitted_code_size_estimate = 10 * (code.size()+1);
	jitted_code_size_estimate += 1024 - (jitted_code_size_estimate%1024); // align on upper 1024 Bytes
	void *m = alloc_writable_memory(jitted_code_size_estimate);
	if (m == 0)
		return 0;

	// 0f b6 07             	movzbl (%rdi),%eax
	// 88 02                	mov    %al,(%rdx)
	// 48 83 c2 01          	add    $0x1,%rdx
	unsigned char asm_put[] = {0x0f, 0xb6, 0x07, 0x88, 0x02, 0x48, 0x83, 0xc2, 0x01};
	// 0f b6 06             	movzbl (%rsi),%eax
	// 48 83 c6 01          	add    $0x1,%rsi
	// 88 07                	mov    %al,(%rdi)
	unsigned char asm_get[] = {0x0f, 0xb6, 0x06, 0x48, 0x83, 0xc6, 0x01, 0x88, 0x07};
	// 80 3f 00             	cmpb   $0x0,(%rdi)
	// 74 09                	je   
	// 48 83 c7 01          	add    $0x1,%rdi
	// 80 3f 00             	cmpb   $0x0,(%rdi)
	// 75 f7                	jne
	unsigned char asm_wind[] = {0x80, 0x3f, 0x00, 0x74, 0x09, 0x48, 0x83, 0xc7, 0x01, 0x80, 0x3f, 0x00, 0x75, 0xf7};
	//	80 3f 00             	cmpb   $0x0,(%rdi)
	//	74 09                	je   
	//	48 83 ef 01          	sub    $0x1,%rdi
	//	80 3f 00             	cmpb   $0x0,(%rdi)
	//	75 f7                	jne
	unsigned char asm_rewd[] = {0x80, 0x3f, 0x00, 0x74, 0x09, 0x48, 0x83, 0xef, 0x01, 0x80, 0x3f, 0x00, 0x75, 0xf7};
	// 80 3f 00             	cmpb   $0x0,(%rdi)
	// 74 09                	je   
	// 48 83 c7 02          	add    $0x2,%rdi
	// 80 3f 00             	cmpb   $0x0,(%rdi)
	// 75 f7                	jne
	unsigned char asm_wind2[] = {0x80, 0x3f, 0x00, 0x74, 0x09, 0x48, 0x83, 0xc7, 0x02, 0x80, 0x3f, 0x00, 0x75, 0xf7};
	//	80 3f 00             	cmpb   $0x0,(%rdi)
	//	74 09                	je   
	//	48 83 ef 02          	sub    $0x2,%rdi
	//	80 3f 00             	cmpb   $0x0,(%rdi)
	//	75 f7                	jne
	unsigned char asm_rewd2[] = {0x80, 0x3f, 0x00, 0x74, 0x09, 0x48, 0x83, 0xef, 0x02, 0x80, 0x3f, 0x00, 0x75, 0xf7};
	// 83 07 54             	addl   $0x54,(%rdi)
	unsigned char asm_add_to_ptee_without_shift[] = {0x83, 0x07, 0x54};
	// c6 47 05 54          	movb   $0x54,0x5(%rdi)
	unsigned char asm_set_with_offset[] = {0xc6, 0x47, 0x05, 0x54};
	// c6 07 00             	movb   $0x0,(%rdi)
	unsigned char asm_set[] = {0xc6, 0x07, 0x00};
	// 80 3f 00             	cmpb   $0x0,(%rdi)
	// 0f 84 xx xx xx xx    	je
	unsigned char asm_jz_first_part[] = {0x80, 0x3f, 0x00, 0x0f, 0x84, 0x00, 0x00, 0x00, 0x00};
	// 80 3f 00             	cmpb   $0x0,(%rdi)
	// 0f 85 xx xx xx xx    	jne
	unsigned char asm_jnz_first_part[] = {0x80, 0x3f, 0x00, 0x0f, 0x85, 0x00, 0x00, 0x00, 0x00};
	// 48 83 c7 xx          	add    $0x1,%rdi
	unsigned char asm_add_to_ptr[] = {0x48, 0x83, 0xc7, 0x00};
	// 80 47 00 xx          	addb   $0xxx,0x00(%rdi)
	unsigned char asm_add_to_ptee[] = {0x80, 0x47, 0x00, 0x00};
	// 48 8b 07             	mov    (%rdi),%rax
	unsigned char asm_mov_ptee_to_al[] = {0x48, 0x8b, 0x07};
	// 48 6b 07 45          	imul   $0x45,(%rdi),%rax
	unsigned char asm_mul_ptee_to_al[] = {0x48, 0x6b, 0x07, 0x00};
	// 00 47 05             	add    %al,0x5(%rdi)
	unsigned char asm_add_al_to_ptee[] = {0x00, 0x47, 0x05};


	std::vector<std::pair<std::vector<std::vector<int>>::iterator, char*>> loop_pos;
	char * ptr = (char*)m;
	for (auto it=ops.begin(),end=ops.end() ; it!=end ; ++it)
	{
		switch ((*it)[0])
		{
			case ADDMAP:
			{
				// pre-adds, before shift
				for (auto i = it->begin() + 2 ; i<it->end() ; i+=2)
				{
					if (*i == 0)
					{
						memcpy(ptr, asm_add_to_ptee_without_shift, sizeof(asm_add_to_ptee_without_shift));
						ptr += sizeof(asm_add_to_ptee_without_shift);
						ptr[-1] = (char)(i[1]);
					}
				}
				// shifted adds
				for (auto i = it->begin() + 2 ; i<it->end() ; i+=2)
				{
					if (*i != 0 && *i != (*it)[1])
					{
						memcpy(ptr, asm_add_to_ptee, sizeof(asm_add_to_ptee));
						ptr += sizeof(asm_add_to_ptee);
						ptr[-2] = (char)(*i);
						ptr[-1] = (char)(i[1]);
					}
				}
				// final shift
				if ((*it)[1] != 0)
				{
					memcpy(ptr, asm_add_to_ptr, sizeof(asm_add_to_ptr));
					ptr += sizeof(asm_add_to_ptr);
					ptr[-1] = (char)((*it)[1]);
					// post adds, after shift, if any
					for (auto i = it->begin() + 2 ; i<it->end() ; i+=2)
					{
						if (*i == (*it)[1])
						{
							memcpy(ptr, asm_add_to_ptee_without_shift, sizeof(asm_add_to_ptee_without_shift));
							ptr += sizeof(asm_add_to_ptee_without_shift);
							ptr[-1] = (char)(i[1]);
						}
					}
				}
				break;
			}
			case MULMAP:
			{
				bool moved_ptee_to_al = false;
				for (auto i = it->begin() + 1 ; i<it->end() ; i+=2)
				{
					if (i[1] == 1)
					{
						if ( ! moved_ptee_to_al)
						{
							memcpy(ptr, asm_mov_ptee_to_al, sizeof(asm_mov_ptee_to_al));
							ptr += sizeof(asm_mov_ptee_to_al);
							moved_ptee_to_al = true;
						}
						memcpy(ptr, asm_add_al_to_ptee, sizeof(asm_add_al_to_ptee));
						ptr += sizeof(asm_add_al_to_ptee);
						ptr[-1] = i[0];
					}
				}
				for (auto i = it->begin() + 1 ; i<it->end() ; i+=2)
				{
					if (i[1] != 1)
					{
						memcpy(ptr, asm_mul_ptee_to_al, sizeof(asm_mul_ptee_to_al));
						ptr += sizeof(asm_mul_ptee_to_al);
						ptr[-1] = i[1];
						
						memcpy(ptr, asm_add_al_to_ptee, sizeof(asm_add_al_to_ptee));
						ptr += sizeof(asm_add_al_to_ptee);
						ptr[-1] = i[0];
					}
				}
				memcpy(ptr, asm_set, sizeof(asm_set));
				ptr += sizeof(asm_set);
				ptr[-1] = 0;
				break;
			}
			case SET:
				if ((*it)[2] == 0)
				{
					memcpy(ptr, asm_set, sizeof(asm_set));
					ptr += sizeof(asm_set);
					ptr[-1] = (char)(*it)[1];
				}
				else
				{
					memcpy(ptr, asm_set_with_offset, sizeof(asm_set_with_offset));
					ptr += sizeof(asm_set_with_offset);
					ptr[-1] = (char)(*it)[1]; // value
					ptr[-2] = (char)(*it)[2]; // offset
				}
				break;
			case OUT:
				memcpy(ptr, asm_put, sizeof(asm_put));
				ptr += sizeof(asm_put);
				//*(int32_t*)(&ptr[-4]) = (char*)put-ptr;
				break;
			case IN:
				memcpy(ptr, asm_get, sizeof(asm_get));
				ptr += sizeof(asm_get);
				//*(int32_t*)(&ptr[-4]) = (char*)get-ptr;
				break;
			case WIND:
				memcpy(ptr, asm_wind, sizeof(asm_wind));
				ptr += sizeof(asm_wind);
				break;
			case REWD:
				memcpy(ptr, asm_rewd, sizeof(asm_rewd));
				ptr += sizeof(asm_rewd);
				break;
			case WIND2:
				memcpy(ptr, asm_wind2, sizeof(asm_wind2));
				ptr += sizeof(asm_wind2);
				break;
			case REWD2:
				memcpy(ptr, asm_rewd2, sizeof(asm_rewd2));
				ptr += sizeof(asm_rewd2);
				break;
			case '[':
				memcpy(ptr, asm_jz_first_part, sizeof(asm_jz_first_part));
				ptr += sizeof(asm_jz_first_part);
				loop_pos.push_back(std::make_pair<std::vector<std::vector<int>>::iterator, char*>(it+1, ptr-4));
				break;
			case ']':
			{
				memcpy(ptr, asm_jnz_first_part, sizeof(asm_jnz_first_part));
				ptr += sizeof(asm_jnz_first_part);
				int jump_size = 0;
				for (auto loopbegin=loop_pos.back().first,loopend=it+1 ; loopbegin < loopend ; ++loopbegin)
				{
					switch ((*loopbegin)[0])
					{
						case SET:
							if ((*loopbegin)[2] == 0)
								jump_size += sizeof(asm_set);
							else
								jump_size += sizeof(asm_set_with_offset);
							break;
						case OUT:
							jump_size += sizeof(asm_put);
							break;
						case IN:
							jump_size += sizeof(asm_get);
							break;
						case WIND:
							jump_size += sizeof(asm_wind);
							break;
						case REWD:
							jump_size += sizeof(asm_rewd);
							break;
						case WIND2:
							jump_size += sizeof(asm_wind2);
							break;
						case REWD2:
							jump_size += sizeof(asm_rewd2);
							break;
						case '[':
							jump_size += sizeof(asm_jz_first_part);
							break;
						case ']':
							jump_size += sizeof(asm_jnz_first_part);
							break;
						case ADDMAP:
							for (auto i = loopbegin->begin() + 2 ; i<loopbegin->end() ; i+=2)
								if (*i == 0)
									jump_size += sizeof(asm_add_to_ptee_without_shift);
							for (auto i = loopbegin->begin() + 2 ; i<loopbegin->end() ; i+=2)
								if (*i != 0 && *i != (*loopbegin)[1])
									jump_size += sizeof(asm_add_to_ptee);
							if ((*loopbegin)[1] != 0)
							{
								jump_size += sizeof(asm_add_to_ptr);
								for (auto i = loopbegin->begin() + 2 ; i<loopbegin->end() ; i+=2)
									if (*i == (*loopbegin)[1])
										jump_size += sizeof(asm_add_to_ptee_without_shift);
							}
							break;
						case MULMAP:
						{
							bool moved_ptee_to_al = false;
							for (auto i = loopbegin->begin() + 1 ; i<loopbegin->end() ; i+=2)
							{
								if (i[1] == 1)
								{
									if ( ! moved_ptee_to_al)
									{
										jump_size += sizeof(asm_mov_ptee_to_al);
										moved_ptee_to_al = true;
									}
									jump_size += sizeof(asm_add_al_to_ptee);
								}
							}
							for (auto i = loopbegin->begin() + 1 ; i<loopbegin->end() ; i+=2)
							{
								if (i[1] != 1)
								{
									jump_size += sizeof(asm_mul_ptee_to_al);
									jump_size += sizeof(asm_add_al_to_ptee);
								}
							}
							jump_size += sizeof(asm_set);
							break;
						}
					}
				}
				*(int32_t*)(&ptr[-4]) = -jump_size;
				*(int32_t*)(loop_pos.back().second) = jump_size;
				loop_pos.pop_back();
				break;
			}
			break;
		}
	}
	*ptr++ = 0xc3; // retq				// return ptruint8_t *

	make_memory_executable(m, jitted_code_size_estimate);
	return (JittedFunc) m;
}

void Run(JittedFunc f)
{
	std::string input;
	input.reserve(10000);
	for (int c=getc(stdin) ; c!=EOF ; c=getc(stdin))
		input.push_back(char(c));
	input.push_back(char(0));
	char * input_ptr = &*input.begin();

	std::vector<char> output(100000, '\0');
	char * output_ptr = &*output.begin();

	std::string memory(3000000, char(0));
	char * mem_ptr = &memory[1000000];

	f(mem_ptr, input_ptr, output_ptr);
	
	printf("%s", output_ptr);
}

