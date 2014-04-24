#ifndef UD_RECTMEMORYMANAGER_HEADER__
#define UD_RECTMEMORYMANAGER_HEADER__

#include "RasterFile.h"
#include <io.h>
#include<string>

const size_t XX_MB_BYTES_COUNT = 1048576;

template <typename T>
class CacheRect
{
public:
	CacheRect(int brow, int bcol, int rownum, int colnum) : brow(brow), bcol(bcol), rownum(rownum), colnum(colnum), data(nullptr)
	{
		data = new T[rownum*colnum]();
	}
	~CacheRect()
	{
		if (data != nullptr)
		{
			delete[] data;
			data = nullptr;
		}
	}
	T* operator()(int rowindex, int colindex)
	{
		if ((rowindex >= brow && rowindex < brow + rownum) && (colindex >= bcol && colindex < bcol + colnum))
		{
			return &data[(rowindex - brow) * colnum + colindex - bcol];
		}
		return nullptr;
	}
	void setRect(int brow, int bcol, int rownum, int colnum)
	{
		this->brow = brow;
		this->bcol = bcol;
		this->rownum = rownum;
		this->colnum = colnum;
	}
	T* Data()
	{
		return data;
	}
	int brow;
	int bcol;
	int rownum;
	int colnum;

private:
	T* data;
};

template <typename T>
class RectMemoryManager
{
public:
	RectMemoryManager(size_t sizecol, size_t sizerow, bool needsync = false, T initvalue = 0, int mode = -1) throw (std::bad_alloc,std::exception);
	~RectMemoryManager();
	void Destroy();
	T& operator[](size_t index);
//	T& at(size_t index);
	bool setData(T* pdata, size_t count);
	bool setData(T* data, size_t brow, size_t rowcount);
	T* getData();
	size_t size()
	{
		return _elemcount;
	}
	bool setStoreMode();
	void setSync(bool isOn)
	{
		_needsync = isOn;
	}
	bool getSync() const
	{
		return _needsync;
	}
	T* getData(size_t brow, size_t bcol, size_t rowcount, size_t colcount);
private:
	void setCacheRect(int brow, int bcol, int rownum, int colnum);
	T* getCacheRect(size_t index);
	void syncToDisk();
	void Init(T initvalue = 0) throw (std::bad_alloc,std::exception);
	void fitRect(int& nowrow, int &nowcol, int& brow, int& bcol);
	T* getModeZeroRect(size_t brow, size_t bcol, size_t rowcount, size_t colcount);
	T* getModeOneRect(size_t brow, size_t bcol, size_t rowcount, size_t colcount);
	T* getModeTwoRect(size_t brow, size_t bcol, size_t rowcount, size_t colcount);

private:
	void CreateDiskFile(); 
		
private:	
	T** _data;
	size_t _sizecol;
	size_t _sizerow;
	size_t _elemcount;
	int _pagecount;
	int _storemode;
	CacheRect<T>* _prect;
	bool _needsync;
	int _memcacherow;
	int _memcachecol;
	char* _path;
	RasterFile* _pTiff;
	static const int _ALLINMEM_MAX_MB = 100;
	static const int _ALLINFILE_MIN_MB = 300;
	static const int XX_PAGE_MEM_MB_SIZE = 4;
	static const size_t XX_PAGE_MEM_MB_ELEM;
};


template <typename T>
const size_t RectMemoryManager<T>::XX_PAGE_MEM_MB_ELEM = XX_MB_BYTES_COUNT * XX_PAGE_MEM_MB_SIZE / sizeof(T);

template <typename T>
RectMemoryManager<T>::RectMemoryManager(size_t sizecol, size_t sizerow, bool needsync, T initvalue, int mode) throw (std::bad_alloc) :
_data(nullptr), _sizecol(sizecol), _sizerow(sizerow), _elemcount(0), _pagecount(0), _storemode(mode), _prect(nullptr), _needsync(needsync), _path(nullptr), _pTiff(nullptr)
{
	_memcachecol = sizecol / 4;
	_memcacherow = sizerow / 4;
	_storemode = mode;
	_elemcount = sizecol * sizerow;
	if (_elemcount > 0)
	{
		try{
			
			Init(initvalue);
		}
		catch (std::bad_alloc& ex)
		{
			Destroy();
			throw;
		}
	}
}

template <typename T>
RectMemoryManager<T>::~RectMemoryManager()
{
	Destroy();
}

template <typename T>
void RectMemoryManager<T>::Destroy()
{
	if (_prect != nullptr)
	{
		delete _prect;
		_prect = nullptr;
	}

	if (_pTiff != nullptr)
	{
		delete _pTiff;
		_pTiff = nullptr;
	}

	if (_path != nullptr)
	{
		if ((_access(_path, 0)) != -1)
		{
			remove(_path);
		}

		delete[] _path;
		_path = nullptr;
	}

	if (_data != nullptr)
	{
		for (int i = 0; i < _pagecount; ++i)
		{
			delete[] _data[i];
			_data[i] = nullptr;
		}
		delete[] _data;
		_data = nullptr;
	}
	_pagecount = 0;
}

template <typename T>
void RectMemoryManager<T>::Init(T initvalue) throw (std::bad_alloc,std::exception)
{
	try
	{
		size_t _size = _elemcount * sizeof(T);
		if ((_storemode == -1 && _size < XX_MB_BYTES_COUNT * _ALLINMEM_MAX_MB) || (_storemode == 0))
		{
#ifdef _DEBUG
			printf("mem mode\n");
#endif // DEBUG

			T* pp = new T[_elemcount]();
			_data = new T*(pp);
			pp = nullptr;
			_pagecount = 1;
			if (initvalue != 0 )
			{
				for (size_t i = 0; i < _elemcount; i++)
				{
					(*_data)[i] = initvalue;
				}
			}
			_storemode = 0;
		}
		else if ((_storemode == -1 && _size < XX_MB_BYTES_COUNT * _ALLINFILE_MIN_MB) || (_storemode == 1))
		{
#ifdef _DEBUG
			printf("page mode \n");
#endif // DEBUG

			int needallocpage = _size / (XX_MB_BYTES_COUNT * XX_PAGE_MEM_MB_SIZE);
			if (_size % (XX_MB_BYTES_COUNT * XX_PAGE_MEM_MB_SIZE) != 0)
			{
				needallocpage += 1;
			}
			int pageavanum = XX_MB_BYTES_COUNT / sizeof(T)* XX_PAGE_MEM_MB_SIZE;
			_data = new T*[needallocpage]();
			for (size_t i = 0; i < needallocpage; i++)
			{

				_data[i] = nullptr;
				_data[i] = new T[pageavanum]();
#ifdef _DEBUG
				printf("page %d \n",i);
				printf("Ox%X\n", _data[i]);
#endif // DEBUG
				_pagecount += 1;
			}
#ifdef _DEBUG
				printf("page new over!\n");
#endif // _DEBUG


			if (initvalue != 0)
			{
				for (size_t i = 0; i < _pagecount; i++)
				{
					for (size_t j = 0; j < pageavanum; j++)
					{
						_data[i][j] = initvalue;
					}
				}
			}
			_storemode = 1;
		}
		else if ((_storemode == -1) || (_storemode == 2)){
#ifdef DEBUG
			printf("file mode\n");
#endif // DEBUG
			{
				time_t tmd;
				time(&tmd);
				std::ostringstream isevenm;
				isevenm << tmd;
				std::string xxx = isevenm.str() + ".tif";
				_path = new char[xxx.length() + 1]();
				memcpy(_path, xxx.c_str(), xxx.length());
				_path[xxx.length()] = '\0';
				CreateDiskFile();
			}
			_pTiff = new RasterFile(string(_path));
			vector<T> tempinitdata(_sizecol, initvalue);
			for (size_t i = 0; i < _sizerow; i++)
			{
				_pTiff->ChangeDataset(&tempinitdata[0], i, 0, 1, _sizecol);
			}
			_storemode = 2;
		}
		else
		{
			throw std::exception(" error special mode or program error!");
		}
	}
	catch (std::bad_alloc& e)
	{
		Destroy();
		++_storemode;
		Init(initvalue);
	}
}

template <>
void RectMemoryManager<int>::CreateDiskFile()
{
	RasterFile::Create(_path, _sizecol, _sizerow, GDT_Int32, nullptr, nullptr);
}

template <>
void RectMemoryManager<short>::CreateDiskFile()
{
	RasterFile::Create(_path, _sizecol, _sizerow, GDT_Int16, nullptr, nullptr);
}

template <>
void RectMemoryManager<float>::CreateDiskFile()
{
	RasterFile::Create(_path, _sizecol, _sizerow, GDT_Float32, nullptr, nullptr);
}

template <>
void RectMemoryManager<double>::CreateDiskFile()
{
	RasterFile::Create(_path, _sizecol, _sizerow, GDT_Float64, nullptr, nullptr);
}

template <typename T>
void RectMemoryManager<T>::CreateDiskFile()
{
	RasterFile::Create(_path, _sizecol, _sizerow, 0, nullptr, nullptr);
}

template <typename T>
bool RectMemoryManager<T>::setData(T* data, size_t count)
{
	if (count != _elemcount)
	{
		return false;
	}
	if (_storemode == 0)
	{
		memcpy(_data[0], data, count * sizeof(T));
	}
	else if (_storemode == 1)
	{
		int i = 0;
		for (; i < _pagecount - 1; ++i)
		{
			memcpy(_data[i], data + i * XX_PAGE_MEM_MB_ELEM, XX_PAGE_MEM_MB_ELEM * sizeof(T));
		}
		memcpy(_data[i], data + i * XX_PAGE_MEM_MB_ELEM, (count % XX_PAGE_MEM_MB_ELEM) * sizeof(T));
	}
	else if (_storemode == 2)
	{
		_pTiff->ChangeDataset(data, 0, 0, _sizerow, _sizecol);
	}
	return true;
}

template <typename T>
bool RectMemoryManager<T>::setData(T* data, size_t brow, size_t rowcount)
{
	if (_storemode == 0)
	{
		memcpy((*_data) + brow*_sizecol, data, rowcount*_sizecol*sizeof(T));
	}
	else if (_storemode == 1 || _storemode == 2)
	{
		size_t beginindex = brow * _sizecol;
		for (size_t i = 0; i < rowcount*_sizecol; i++)
		{
			(*this)[beginindex + i] = data[i];
		}
	}
	else
	{
		return false;
	}
	return true;
}


template <typename T>
T* RectMemoryManager<T>::getData()
{
	T* pvalue = nullptr;
	try
	{
		pvalue = new T[_elemcount];
		if (_storemode == 0)
		{
			memcpy(pvalue, *_data, _elemcount*sizeof(T));
			return pvalue;
		}
		else if (_storemode == 1)
		{
			size_t i = 0;
			for (; i < _pagecount - 1; i++)
			{
				memcpy(pvalue + i * XX_PAGE_MEM_MB_ELEM, _data[i], XX_PAGE_MEM_MB_ELEM * sizeof(T));
			}
			memcpy(pvalue + i * XX_PAGE_MEM_MB_ELEM, _data[i], (_elemcount % XX_PAGE_MEM_MB_ELEM) * sizeof(T));
		}
		else if (_storemode == 2)
		{
			_pTiff->ReadTiffDataset(pvalue, 0, 0, _sizecol, _sizerow);
		}
	}
	catch (std::bad_alloc& ex)
	{
		if (pvalue != nullptr)
		{
			delete[] pvalue;
			pvalue = nullptr;
		}
		printf("in getData: %s\n", ex.what());
	}
	return pvalue;
}

template <typename T>
T* RectMemoryManager<T>::getModeZeroRect(size_t brow, size_t bcol, size_t rowcount, size_t colcount)
{

	T* pvalue = new T[rowcount*colcount]();
	for (size_t i = 0; i < rowcount; i++)
	{
		memcpy(pvalue + colcount*i, (*_data) + (i + brow)*_sizecol + bcol, colcount*sizeof(T));
	}
	return pvalue;
}

template <typename T>
T* RectMemoryManager<T>::getModeOneRect(size_t brow, size_t bcol, size_t rowcount, size_t colcount)
{
	T* pvalue = new T[rowcount*colcount]();
	for (size_t i = 0; i < rowcount; i++)
	{
		for (size_t j = 0; j < colcount; j++)
		{
			pvalue[i*colcount + j] = (*this)[(brow + i)*_sizecol + bcol + j];
		}
	}

	return pvalue;
}

template <typename T>
T* RectMemoryManager<T>::getModeTwoRect(size_t brow, size_t bcol, size_t rowcount, size_t colcount)
{
	T* pvalue = new T[rowcount*colcount]();
	_pTiff->ReadTiffDataset(pvalue, brow, bcol, rowcount, colcount);
	return pvalue;
}

template <typename T>
T* RectMemoryManager<T>::getData(size_t brow, size_t bcol, size_t rowcount, size_t colcount)
{
	if (rowcount + brow > _sizerow || colcount + bcol > _sizecol)
	{
		return nullptr;
	}
	T* pvalue = nullptr;
	try
	{
		if (_storemode == 0)
		{
			pvalue = getModeZeroRect(brow, bcol, rowcount, colcount);
		}
		else if (_storemode == 1)
		{
			pvalue = getModeOneRect(brow, bcol, rowcount, colcount);
		}
		else if (_storemode == 2)
		{
			pvalue = getModeTwoRect(brow, bcol, rowcount, colcount);
		}
	}
	catch (std::bad_alloc& ex)
	{
		if (pvalue != nullptr)
		{
			delete[] pvalue;
			pvalue = nullptr;
		}
		printf("in getData(size_t,size_t...): %s\n", ex.what());
	}
	return pvalue;
}

template <typename T>
T& RectMemoryManager<T>::operator[](size_t index)
{
	if (index >= _elemcount)
	{
		exit(-1);
	}

	if (_storemode == 0)
	{
		return _data[0][index];
	}
	else if (_storemode == 1)
	{
		return _data[index / XX_PAGE_MEM_MB_ELEM][index % XX_PAGE_MEM_MB_ELEM];
	}
	else if (_storemode == 2)
	{
		return *getCacheRect(index);
	}
	else
	{
		printf("in RectMemoryManager::[] : wocao!\n");
		Destroy();
		exit(-1);
	}
}

template <typename T>
void RectMemoryManager<T>::setCacheRect(int brow,int bcol,int rownum,int colnum)
{
	_prect->setRect(brow, bcol, rownum, colnum);
	_pTiff->ReadTiffDataset(_prect->Data(), brow, bcol, rownum, colnum);
}

template <typename T>
void RectMemoryManager<T>::fitRect(int& nowrow, int& nowcol, int& brow, int& bcol)
{
	brow = nowrow - _memcacherow / 2;
	bcol = nowcol - _memcachecol / 2;
	brow = brow < 0 ? 0 : brow;
	brow = brow > _sizerow - _memcacherow ? _sizerow - _memcacherow : brow;
	bcol = bcol < 0 ? 0 : bcol;
	bcol = bcol > _sizecol - _memcachecol ? _sizecol - _memcachecol : bcol;
}

template <typename T>
T* RectMemoryManager<T>::getCacheRect(size_t index)
{
	int nowrow = index / _sizecol;
	int nowcol = index % _sizecol;
	int brow(0); 
	int bcol(0); 

	T* pvalue = nullptr;
	if (_prect == nullptr)
	{
		fitRect(nowrow, nowcol, brow, bcol);
		_prect = new CacheRect<T>(brow,bcol,_memcacherow,_memcachecol);
		setCacheRect(brow,bcol,_memcacherow,_memcachecol);
	}
	if ((pvalue = (*_prect)(nowrow, nowcol)) == nullptr)
	{
		fitRect(nowrow, nowcol, brow, bcol);
		if (_needsync)
		{
			syncToDisk();
		}
		setCacheRect(brow, bcol, _memcacherow, _memcachecol);
		pvalue = (*_prect)(nowrow, nowcol);
	}
	return pvalue;
}


template <typename T>
void RectMemoryManager<T>::syncToDisk()
{
	_pTiff->ChangeDataset(_prect->Data(), _prect->brow, _prect->bcol, _prect->rownum, _prect->colnum);
}

#endif
