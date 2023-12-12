import urllib
from urllib.request import urlopen

from pprint import pprint
import json
import logging

ch = logging.StreamHandler()
ch.setLevel(logging.DEBUG)
formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
ch.setFormatter(formatter)


# *************************************************************************** #
#
class Assembly64(object):

    URLBASE = "http://hackerswithstyle.se/leet"
    # ********************************************************************* #
    #
    def __init__(self, url = URLBASE):
        """ Constructor. """

        self.logger = logging.getLogger(self.__class__.__name__)
        self.logger.addHandler(ch)
        self.logger.setLevel(logging.DEBUG)
        self.url = url
        
    # ********************************************************************* #
    #
    def _exchange(self, api_url, data=None, formdata=None, content_type="application/json", method = None, timeout=50, cookie = None):
        """ Send a command and receive the response. """
        assert timeout is not None # No timeout may cause the test scripts to hang!!!
    
        # Create the request.
        url = self.url + api_url

        if formdata != None:
            url += "?" + urllib.parse.urlencode(formdata)

        self.logger.debug("Opening URL '%s'." % url)

        request = urllib.request.Request(url, method = method)
        if data is not None:
            self.logger.debug("Content-Type is '%s'." % content_type)
            if content_type == "application/json":
                self.logger.debug("POST data is '%s'." % data)
            request.add_header('Content-Type', content_type)

        if cookie:
            request.add_header('cookie', cookie)
            request.add_header('origin', URLBASE)
            request.add_header('accept', 'text/html')
            request.add_header('cache-control', 'max-age=0')

        # Send the request and receive the response.
        try:
            if data != None:
                data = data.encode('utf-8')
            response = urlopen(request, data, timeout)
        except urllib.request.HTTPError as e:
            self.logger.error("HTTP error, code: %d, reason: %s" % (e.code, e.reason))
            error_body = e.read()
            if error_body:
                self.logger.error("HTTP error body:\n" + error_body)
            else:
                self.logger.info("(No HTTP error body.)")
            raise
        response_data = response.read()
        response.close()
    
        # Parse and return the response.
        if response_data:
            if content_type == "application/json":
                return json.loads(response_data.decode('utf-8'))
            elif content_type == "application/octet-stream":
                return response_data
            else: # Text?
                return response_data.decode('utf-8')

    def build_query(dct):
        # name, group, handle, event, date*, category*, subcat*, rating*, type*, repo*, latest, sort, order
        quoted = [ 'name', 'group', 'handle', 'event']
        query = []
        for k in dct.keys():
            if k in quoted:
                query.append('({0:s}:"{1:s}")'.format(k,dct[k]))
            else:
                query.append('({0:s}:{1:s})'.format(k,dct[k]))
        return " & ".join(query)



a = Assembly64()

presets = a._exchange("/search/aql/presets")
pprint(presets)
#for d in presets:
#    print(d['type'])



game = input("Give name of game: ")
search = { 'name' : game, 'type' : 'd64', 'category': 'games'}
query = Assembly64.build_query(search)
results = a._exchange("/search/aql", formdata = { 'query' : query } )

for (i,el) in enumerate(results):
    group = '({0:s})'.format(el['group']) if 'group' in el else ''
    print("{0:d}: {1:s} {2:s}".format(i, el['name'], group))

n = int(input("Your choice: "))

choice = results[n]
pprint(choice)

entries = a._exchange("/search/entries/{0:s}/{1:d}".format(choice['id'], choice['category']))

pprint(entries)

if 'contentEntry' in entries:
    data_items = entries['contentEntry']
    for item in data_items:
        data = a._exchange("/search/bin/{0:s}/{1:d}/{2:d}".format(choice['id'], choice['category'], item['id']), content_type="application/octet-stream")
        with open(item['path'], 'wb') as f:
            f.write(data)
        print(item['path'], len(data))
